#ifndef MARKET_DATA_HPP
#define MARKET_DATA_HPP

#include <string>

struct MarketData {
    std::string token;
    int subscriptionMode;
    double ltp;
    long long volume;
    long long openInterest;
    long long lastTradeQty;
    bool wasAggressorBuy;
};

#endif
