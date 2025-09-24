#include "binance_client.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

std::string to_upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

bool parse_bool(const std::string& value) {
    const std::string upper = to_upper(value);
    return upper == "1" || upper == "TRUE" || upper == "YES" || upper == "ON";
}

void print_usage() {
    std::cout << "Usage:\n"
              << "  call_api_test klines <PAIR> <INTERVAL> [LIMIT] [CONTRACT_TYPE]\n"
              << "  call_api_test set-leverage <SYMBOL> <LEVERAGE>\n"
              << "  call_api_test place-order <SYMBOL> <SIDE> <TYPE> [options]\n"
              << "      Options: --quantity <qty> --quoteQty <qty> --price <price> --timeInForce <GTC|IOC|FOK|GTX>\n"
              << "               --reduceOnly [true|false] --positionSide <BOTH|LONG|SHORT> --clientOrderId <id>\n"
              << "               --stopPrice <price> --stopLoss <price> --takeProfit <price>\n"
              << "  call_api_test open-orders [SYMBOL]\n"
              << "  call_api_test all-orders <SYMBOL> [LIMIT]\n"
              << "  call_api_test account\n"
              << "  call_api_test position-risk [SYMBOL]\n"
              << "  call_api_test funding-rate <SYMBOL> [LIMIT]\n"
              << "  call_api_test funding-fee <SYMBOL> [LIMIT]\n"
              << "  call_api_test close-position <SYMBOL>\n"
              << "  call_api_test status <SYMBOL>\n";
}

BinanceFuturesClient::Side parse_side(const std::string& value) {
    const std::string upper = to_upper(value);
    if (upper == "BUY") {
        return BinanceFuturesClient::Side::BUY;
    }
    if (upper == "SELL") {
        return BinanceFuturesClient::Side::SELL;
    }
    throw std::runtime_error("Unsupported side: " + value);
}

BinanceFuturesClient::OrderType parse_order_type(const std::string& value) {
    const std::string upper = to_upper(value);
    if (upper == "MARKET") {
        return BinanceFuturesClient::OrderType::MARKET;
    }
    if (upper == "LIMIT") {
        return BinanceFuturesClient::OrderType::LIMIT;
    }
    if (upper == "STOP_MARKET") {
        return BinanceFuturesClient::OrderType::STOP_MARKET;
    }
    if (upper == "TAKE_PROFIT_MARKET") {
        return BinanceFuturesClient::OrderType::TAKE_PROFIT_MARKET;
    }
    throw std::runtime_error("Unsupported order type: " + value);
}

BinanceFuturesClient::TimeInForce parse_time_in_force(const std::string& value) {
    const std::string upper = to_upper(value);
    if (upper == "GTC") {
        return BinanceFuturesClient::TimeInForce::GTC;
    }
    if (upper == "IOC") {
        return BinanceFuturesClient::TimeInForce::IOC;
    }
    if (upper == "FOK") {
        return BinanceFuturesClient::TimeInForce::FOK;
    }
    if (upper == "GTX") {
        return BinanceFuturesClient::TimeInForce::GTX;
    }
    throw std::runtime_error("Unsupported time-in-force: " + value);
}

std::map<std::string, std::string> parse_options(int startIndex, int argc, char* argv[]) {
    std::map<std::string, std::string> options;
    for (int i = startIndex; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) != 0) {
            throw std::runtime_error("Unexpected argument: " + arg);
        }
        std::string key = arg.substr(2);
        std::string value = "true";
        if (i + 1 < argc) {
            std::string next = argv[i + 1];
            if (next.rfind("--", 0) != 0) {
                value = next;
                ++i;
            }
        }
        options[key] = value;
    }
    return options;
}

void print_json(const json& value) {
    std::cout << value.dump(2) << std::endl;
}

bool read_use_testnet_from_env() {
    const char* env = std::getenv("BINANCE_USE_TESTNET");
    if (!env) {
        return true;
    }
    const std::string value = to_upper(env);
    return !(value == "0" || value == "FALSE" || value == "NO" || value == "OFF");
}

BinanceFuturesClient create_public_client() {
    return BinanceFuturesClient("", "", read_use_testnet_from_env());
}

BinanceFuturesClient create_private_client(const char* apiKey, const char* apiSecret) {
    return BinanceFuturesClient(apiKey ? apiKey : "", apiSecret ? apiSecret : "", read_use_testnet_from_env());
}

}  // namespace

int call_api_demo(int argc, char* argv[]) {
    try {
        if (argc == 1) {
            print_usage();
            std::cout << "\nFetching latest ETHUSDT 1m perpetual contract candles..." << std::endl;
            BinanceFuturesClient publicClient = create_public_client();
            auto candles = publicClient.getContinuousKlines("ETHUSDT", "1m", 5);
            print_json(candles);

            const char* apiKey = std::getenv("BINANCE_API_KEY");
            const char* apiSecret = std::getenv("BINANCE_API_SECRET");
            if (apiKey && apiSecret) {
                BinanceFuturesClient client = create_private_client(apiKey, apiSecret);
                json summary;
                summary["account"] = client.getAccountInfo();
                summary["positions"] = client.getPositionRisk("ETHUSDT");
                summary["funding"] = client.getFundingRate("ETHUSDT", 1);
                print_json(summary);
            } else {
                std::cout << "\nSet BINANCE_API_KEY and BINANCE_API_SECRET environment variables to enable trading commands." << std::endl;
            }
            return 0;
        }

        const std::string command = argv[1];

        if (command == "klines") {
            if (argc < 4) {
                throw std::runtime_error("klines requires at least <PAIR> and <INTERVAL>");
            }
            const std::string pair = argv[2];
            const std::string interval = argv[3];
            int limit = 500;
            if (argc >= 5) {
                limit = std::stoi(argv[4]);
            }
            std::string contractType = "PERPETUAL";
            if (argc >= 6) {
                contractType = argv[5];
            }
            BinanceFuturesClient publicClient = create_public_client();
            auto data = publicClient.getContinuousKlines(pair, interval, limit, contractType);
            print_json(data);
            return 0;
        }

        const char* apiKey = std::getenv("BINANCE_API_KEY");
        const char* apiSecret = std::getenv("BINANCE_API_SECRET");
        if (!apiKey || !apiSecret) {
            throw std::runtime_error("BINANCE_API_KEY and BINANCE_API_SECRET must be set for this command");
        }

        BinanceFuturesClient client = create_private_client(apiKey, apiSecret);

        if (command == "set-leverage") {
            if (argc < 4) {
                throw std::runtime_error("set-leverage requires <SYMBOL> and <LEVERAGE>");
            }
            const std::string symbol = argv[2];
            int leverage = std::stoi(argv[3]);
            auto response = client.setLeverage(symbol, leverage);
            print_json(response);
            return 0;
        }
        if (command == "place-order") {
            if (argc < 5) {
                throw std::runtime_error("place-order requires <SYMBOL> <SIDE> <TYPE>");
            }
            BinanceFuturesClient::OrderRequest request;
            request.symbol = argv[2];
            request.side = parse_side(argv[3]);
            request.type = parse_order_type(argv[4]);

            auto options = parse_options(5, argc, argv);
            if (auto it = options.find("quantity"); it != options.end()) {
                request.quantity = std::stod(it->second);
            }
            if (auto it = options.find("quoteQty"); it != options.end()) {
                request.quoteOrderQty = std::stod(it->second);
            }
            if (auto it = options.find("price"); it != options.end()) {
                request.price = std::stod(it->second);
            }
            if (auto it = options.find("timeInForce"); it != options.end()) {
                request.timeInForce = parse_time_in_force(it->second);
            }
            if (auto it = options.find("reduceOnly"); it != options.end()) {
                request.reduceOnly = parse_bool(it->second);
            }
            if (auto it = options.find("positionSide"); it != options.end()) {
                request.positionSide = it->second;
            }
            if (auto it = options.find("clientOrderId"); it != options.end()) {
                request.clientOrderId = it->second;
            }
            if (auto it = options.find("stopPrice"); it != options.end()) {
                request.stopPrice = std::stod(it->second);
            }
            if (auto it = options.find("stopLoss"); it != options.end()) {
                request.stopLossPrice = std::stod(it->second);
            }
            if (auto it = options.find("takeProfit"); it != options.end()) {
                request.takeProfitPrice = std::stod(it->second);
            }

            auto response = client.placeOrder(request);
            print_json(response);
            return 0;
        }
        if (command == "open-orders") {
            std::string symbol;
            if (argc >= 3) {
                symbol = argv[2];
            }
            auto response = client.getOpenOrders(symbol);
            print_json(response);
            return 0;
        }
        if (command == "all-orders") {
            if (argc < 3) {
                throw std::runtime_error("all-orders requires <SYMBOL>");
            }
            const std::string symbol = argv[2];
            int limit = 500;
            if (argc >= 4) {
                limit = std::stoi(argv[3]);
            }
            auto response = client.getAllOrders(symbol, limit);
            print_json(response);
            return 0;
        }
        if (command == "account") {
            auto response = client.getAccountInfo();
            print_json(response);
            return 0;
        }
        if (command == "position-risk") {
            std::string symbol;
            if (argc >= 3) {
                symbol = argv[2];
            }
            auto response = client.getPositionRisk(symbol);
            print_json(response);
            return 0;
        }
        if (command == "funding-rate") {
            if (argc < 3) {
                throw std::runtime_error("funding-rate requires <SYMBOL>");
            }
            const std::string symbol = argv[2];
            int limit = 1;
            if (argc >= 4) {
                limit = std::stoi(argv[3]);
            }
            auto response = client.getFundingRate(symbol, limit);
            print_json(response);
            return 0;
        }
        if (command == "funding-fee") {
            if (argc < 3) {
                throw std::runtime_error("funding-fee requires <SYMBOL>");
            }
            const std::string symbol = argv[2];
            int limit = 10;
            if (argc >= 4) {
                limit = std::stoi(argv[3]);
            }
            auto response = client.getFundingFeeHistory(symbol, limit);
            print_json(response);
            return 0;
        }
        if (command == "close-position") {
            if (argc < 3) {
                throw std::runtime_error("close-position requires <SYMBOL>");
            }
            const std::string symbol = argv[2];
            auto response = client.closePosition(symbol);
            print_json(response);
            return 0;
        }
        if (command == "status") {
            if (argc < 3) {
                throw std::runtime_error("status requires <SYMBOL>");
            }
            const std::string symbol = argv[2];
            json result;
            result["symbol"] = symbol;
            result["positionRisk"] = client.getPositionRisk(symbol);
            result["openOrders"] = client.getOpenOrders(symbol);
            result["fundingRate"] = client.getFundingRate(symbol, 1);
            result["fundingFee"] = client.getFundingFeeHistory(symbol, 10);
            print_json(result);
            return 0;
        }

        throw std::runtime_error("Unknown command: " + command);
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
