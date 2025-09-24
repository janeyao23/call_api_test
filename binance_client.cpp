#include "binance_client.hpp"

#include <curl/curl.h>
#include <openssl/hmac.h>

#include <chrono>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {
size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t totalSize = size * nmemb;
    auto* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

long long current_timestamp_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string uppercase(std::string value) {
    for (auto& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string pick_position_side(const json& position) {
    if (position.contains("positionSide")) {
        return position.at("positionSide").get<std::string>();
    }
    return "BOTH";
}
}  // namespace

BinanceFuturesClient::BinanceFuturesClient(std::string apiKey,
                                           std::string secretKey,
                                           bool useTestnet,
                                           long recvWindow)
    : apiKey_(std::move(apiKey)),
      secretKey_(std::move(secretKey)),
      baseUrl_(useTestnet ? "https://testnet.binancefuture.com" : "https://fapi.binance.com"),
      recvWindow_(recvWindow) {
}

std::string BinanceFuturesClient::buildQuery(void* curlHandle, const Params& params) const {
    std::string query;
    CURL* curl = static_cast<CURL*>(curlHandle);
    for (const auto& [key, value] : params) {
        if (value.empty()) {
            continue;
        }
        char* encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
        if (!encoded) {
            throw std::runtime_error("Failed to URL encode parameter");
        }
        if (!query.empty()) {
            query.push_back('&');
        }
        query += key;
        query.push_back('=');
        query += encoded;
        curl_free(encoded);
    }
    return query;
}

std::string BinanceFuturesClient::sign(const std::string& payload) const {
    unsigned int len = 0;
    const unsigned char* result = HMAC(EVP_sha256(), secretKey_.data(), static_cast<int>(secretKey_.size()),
                                       reinterpret_cast<const unsigned char*>(payload.data()), payload.size(), nullptr, &len);
    if (!result) {
        throw std::runtime_error("Failed to sign payload");
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(result[i]);
    }
    return oss.str();
}

json BinanceFuturesClient::performRequest(const std::string& method,
                                          const std::string& path,
                                          const Params& params,
                                          bool isSigned) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialise curl");
    }

    std::string url = baseUrl_ + path;
    std::string body;

    std::string query = buildQuery(curl, params);

    std::string signedQuery = query;
    if (isSigned) {
        if (apiKey_.empty() || secretKey_.empty()) {
            curl_easy_cleanup(curl);
            throw std::runtime_error("API key and secret are required for private endpoints");
        }
        if (!signedQuery.empty()) {
            signedQuery.push_back('&');
        }
        signedQuery += "timestamp=" + std::to_string(current_timestamp_ms());
        if (recvWindow_ > 0) {
            signedQuery += "&recvWindow=" + std::to_string(recvWindow_);
        }
        const std::string signature = sign(signedQuery);
        signedQuery += "&signature=" + signature;
    }

    if (method == "GET" || method == "DELETE") {
        if (!signedQuery.empty()) {
            url += "?";
            url += signedQuery;
        }
    } else {
        body = isSigned ? signedQuery : query;
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    struct curl_slist* headers = nullptr;
    if (isSigned && !apiKey_.empty()) {
        std::string header = "X-MBX-APIKEY: " + apiKey_;
        headers = curl_slist_append(headers, header.c_str());
    }

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    }

    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl);

    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);

    if (headers) {
        curl_slist_free_all(headers);
    }

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::ostringstream oss;
        oss << "curl_easy_perform() failed: " << curl_easy_strerror(res);
        throw std::runtime_error(oss.str());
    }

    if (httpStatus >= 400) {
        std::ostringstream oss;
        oss << "HTTP error " << httpStatus << ": " << response;
        throw std::runtime_error(oss.str());
    }

    if (response.empty()) {
        return json::object();
    }

    return json::parse(response);
}

std::string BinanceFuturesClient::toString(Side side) {
    return side == Side::BUY ? "BUY" : "SELL";
}

std::string BinanceFuturesClient::toString(OrderType type) {
    switch (type) {
        case OrderType::MARKET:
            return "MARKET";
        case OrderType::LIMIT:
            return "LIMIT";
        case OrderType::STOP_MARKET:
            return "STOP_MARKET";
        case OrderType::TAKE_PROFIT_MARKET:
            return "TAKE_PROFIT_MARKET";
        default:
            throw std::runtime_error("Unsupported order type");
    }
}

std::string BinanceFuturesClient::toString(TimeInForce tif) {
    switch (tif) {
        case TimeInForce::GTC:
            return "GTC";
        case TimeInForce::IOC:
            return "IOC";
        case TimeInForce::FOK:
            return "FOK";
        case TimeInForce::GTX:
            return "GTX";
        default:
            throw std::runtime_error("Unsupported time-in-force");
    }
}

std::string BinanceFuturesClient::formatDouble(double value) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(8) << value;
    std::string str = oss.str();
    auto pos = str.find_last_not_of('0');
    if (pos != std::string::npos) {
        if (str[pos] == '.') {
            str.erase(pos);
        } else {
            str.erase(pos + 1);
        }
    }
    if (str.empty()) {
        return "0";
    }
    return str;
}

json BinanceFuturesClient::getContinuousKlines(const std::string& pair,
                                               const std::string& interval,
                                               int limit,
                                               const std::string& contractType) {
    Params params{
        {"pair", pair},
        {"contractType", contractType},
        {"interval", interval},
        {"limit", std::to_string(limit)}
    };
    return performRequest("GET", "/fapi/v1/continuousKlines", params, false);
}

json BinanceFuturesClient::setLeverage(const std::string& symbol, int leverage) {
    Params params{
        {"symbol", uppercase(symbol)},
        {"leverage", std::to_string(leverage)}
    };
    return performRequest("POST", "/fapi/v1/leverage", params, true);
}

json BinanceFuturesClient::placeOrder(const OrderRequest& request) {
    Params params{
        {"symbol", uppercase(request.symbol)},
        {"side", toString(request.side)},
        {"type", toString(request.type)}
    };

    if (request.quantity) {
        params.emplace_back("quantity", formatDouble(*request.quantity));
    }
    if (request.quoteOrderQty) {
        params.emplace_back("quoteOrderQty", formatDouble(*request.quoteOrderQty));
    }
    if (request.price) {
        params.emplace_back("price", formatDouble(*request.price));
    }
    if (request.timeInForce) {
        params.emplace_back("timeInForce", toString(*request.timeInForce));
    }
    if (request.reduceOnly) {
        params.emplace_back("reduceOnly", *request.reduceOnly ? "true" : "false");
    }
    if (request.positionSide) {
        params.emplace_back("positionSide", uppercase(*request.positionSide));
    }
    if (request.clientOrderId) {
        params.emplace_back("newClientOrderId", *request.clientOrderId);
    }
    if (request.stopPrice) {
        params.emplace_back("stopPrice", formatDouble(*request.stopPrice));
    }

    if (!request.quantity && !request.quoteOrderQty) {
        throw std::runtime_error("Either quantity or quoteOrderQty must be provided");
    }

    json result;
    json entry = performRequest("POST", "/fapi/v1/order", params, true);
    result["entry"] = entry;

    const auto executedQty = entry.value("executedQty", entry.value("origQty", std::string{}));

    auto createOcoOrder = [&](const std::optional<double>& priceValue, const std::string& orderType) -> std::optional<json> {
        if (!priceValue) {
            return std::nullopt;
        }
        Params extra{
            {"symbol", uppercase(request.symbol)},
            {"side", request.side == Side::BUY ? "SELL" : "BUY"},
            {"type", orderType},
            {"stopPrice", formatDouble(*priceValue)},
            {"reduceOnly", "true"},
            {"workingType", "MARK_PRICE"}
        };

        if (!executedQty.empty()) {
            extra.emplace_back("quantity", executedQty);
        } else if (request.quantity) {
            extra.emplace_back("quantity", formatDouble(*request.quantity));
        } else {
            throw std::runtime_error("Unable to determine quantity for protective order");
        }

        if (request.positionSide) {
            extra.emplace_back("positionSide", uppercase(*request.positionSide));
        }

        return performRequest("POST", "/fapi/v1/order", extra, true);
    };

    if (auto stopLoss = createOcoOrder(request.stopLossPrice, "STOP_MARKET")) {
        result["stopLoss"] = *stopLoss;
    }
    if (auto takeProfit = createOcoOrder(request.takeProfitPrice, "TAKE_PROFIT_MARKET")) {
        result["takeProfit"] = *takeProfit;
    }

    return result;
}

json BinanceFuturesClient::getOpenOrders(const std::string& symbol) {
    Params params;
    if (!symbol.empty()) {
        params.emplace_back("symbol", uppercase(symbol));
    }
    return performRequest("GET", "/fapi/v1/openOrders", params, true);
}

json BinanceFuturesClient::getAllOrders(const std::string& symbol, int limit) {
    Params params{
        {"symbol", uppercase(symbol)},
        {"limit", std::to_string(limit)}
    };
    return performRequest("GET", "/fapi/v1/allOrders", params, true);
}

json BinanceFuturesClient::getAccountInfo() {
    Params params;
    return performRequest("GET", "/fapi/v2/account", params, true);
}

json BinanceFuturesClient::getPositionRisk(const std::string& symbol) {
    Params params;
    if (!symbol.empty()) {
        params.emplace_back("symbol", uppercase(symbol));
    }
    return performRequest("GET", "/fapi/v2/positionRisk", params, true);
}

json BinanceFuturesClient::getFundingRate(const std::string& symbol, int limit) {
    Params params{
        {"symbol", uppercase(symbol)},
        {"limit", std::to_string(limit)}
    };
    return performRequest("GET", "/fapi/v1/fundingRate", params, false);
}

json BinanceFuturesClient::getFundingFeeHistory(const std::string& symbol, int limit) {
    Params params{
        {"symbol", uppercase(symbol)},
        {"incomeType", "FUNDING_FEE"},
        {"limit", std::to_string(limit)}
    };
    return performRequest("GET", "/fapi/v1/income", params, true);
}

json BinanceFuturesClient::closePosition(const std::string& symbol) {
    json positions = getPositionRisk(symbol);
    if (!positions.is_array()) {
        throw std::runtime_error("Unexpected response for position risk");
    }

    std::string normalisedSymbol = uppercase(symbol);
    std::string closeSide;
    std::string quantity;
    std::optional<std::string> positionSide;

    for (const auto& pos : positions) {
        if (!pos.contains("symbol")) {
            continue;
        }
        if (uppercase(pos.at("symbol").get<std::string>()) != normalisedSymbol) {
            continue;
        }
        const double positionAmt = std::stod(pos.value("positionAmt", "0"));
        if (std::fabs(positionAmt) < 1e-12) {
            continue;
        }
        closeSide = positionAmt > 0 ? "SELL" : "BUY";
        quantity = formatDouble(std::fabs(positionAmt));
        const std::string sideValue = pick_position_side(pos);
        if (sideValue != "BOTH") {
            positionSide = sideValue;
        }
        break;
    }

    if (closeSide.empty() || quantity.empty()) {
        return json{{"symbol", normalisedSymbol}, {"message", "No open position"}};
    }

    Params params{
        {"symbol", normalisedSymbol},
        {"side", closeSide},
        {"type", "MARKET"},
        {"quantity", quantity},
        {"reduceOnly", "true"}
    };

    if (positionSide) {
        params.emplace_back("positionSide", uppercase(*positionSide));
    }

    json response = performRequest("POST", "/fapi/v1/order", params, true);
    return json{{"symbol", normalisedSymbol}, {"close", response}};
}
