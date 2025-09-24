#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

class BinanceFuturesClient {
public:
    enum class Side { BUY, SELL };
    enum class OrderType {
        MARKET,
        LIMIT,
        STOP_MARKET,
        TAKE_PROFIT_MARKET
    };
    enum class TimeInForce {
        GTC,
        IOC,
        FOK,
        GTX
    };

    struct OrderRequest {
        std::string symbol;
        Side side = Side::BUY;
        OrderType type = OrderType::MARKET;
        std::optional<double> quantity;
        std::optional<double> quoteOrderQty;
        std::optional<double> price;
        std::optional<TimeInForce> timeInForce;
        std::optional<bool> reduceOnly;
        std::optional<std::string> positionSide;
        std::optional<std::string> clientOrderId;
        std::optional<double> stopPrice;
        std::optional<double> takeProfitPrice;
        std::optional<double> stopLossPrice;
    };

    BinanceFuturesClient(std::string apiKey, std::string secretKey, bool useTestnet = true, long recvWindow = 5000);

    nlohmann::json getContinuousKlines(const std::string& pair,
                                       const std::string& interval,
                                       int limit = 500,
                                       const std::string& contractType = "PERPETUAL");

    nlohmann::json setLeverage(const std::string& symbol, int leverage);

    nlohmann::json placeOrder(const OrderRequest& request);

    nlohmann::json getOpenOrders(const std::string& symbol = "");

    nlohmann::json getAllOrders(const std::string& symbol, int limit = 500);

    nlohmann::json getAccountInfo();

    nlohmann::json getPositionRisk(const std::string& symbol = "");

    nlohmann::json getFundingRate(const std::string& symbol, int limit = 1);

    nlohmann::json getFundingFeeHistory(const std::string& symbol, int limit = 10);

    nlohmann::json closePosition(const std::string& symbol);

private:
    using Params = std::vector<std::pair<std::string, std::string>>;

    nlohmann::json performRequest(const std::string& method,
                                  const std::string& path,
                                  const Params& params,
                                  bool isSigned);

    std::string buildQuery(void* curlHandle, const Params& params) const;

    std::string sign(const std::string& payload) const;

    static std::string toString(Side side);
    static std::string toString(OrderType type);
    static std::string toString(TimeInForce tif);

    static std::string formatDouble(double value);

    std::string apiKey_;
    std::string secretKey_;
    std::string baseUrl_;
    long recvWindow_;
};
