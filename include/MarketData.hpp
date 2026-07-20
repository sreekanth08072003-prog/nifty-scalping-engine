#ifndef MARKET_DATA_HPP
#define MARKET_DATA_HPP
#include <string>

struct MarketData {
    std::string token;
    double ltp = 0.0;
    double lastTradeQty = 0.0;
};

#endif