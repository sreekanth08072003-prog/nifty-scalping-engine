#ifndef STRATEGY_ENGINE_HPP
#define STRATEGY_ENGINE_HPP

#include "MarketData.hpp"
#include <map>
#include <iostream>
#include <cmath>
#include "OrderManager.hpp"
#include "Config.hpp"

class StrategyEngine {
private:
    struct SymbolState {
        double lastPrice = 0.0;
        double ofiAccumulator = 0.0;
    };
    std::map<std::string, SymbolState> states;

public:
    void onTick(const MarketData& md, OrderManager& om, const std::string& jwt, const std::string& apiKey) {
        auto& state = states[md.token];

        // Log every tick for visibility
        om.logTick(md.token, md.ltp, static_cast<int>(md.lastTradeQty));

        if (state.lastPrice > 0) {
            // Momentum Logic: Price movement * Volume impact
            double priceChange = md.ltp - state.lastPrice;
            
            if (priceChange > 0) {
                state.ofiAccumulator += md.lastTradeQty;
            } else if (priceChange < 0) {
                state.ofiAccumulator -= md.lastTradeQty;
            }
            
            if (std::abs(state.ofiAccumulator) > (Config::OFI_THRESHOLD / 3.0)) {
                std::cout << "[DEBUG] OFI Accumulator for " << md.token << ": " << state.ofiAccumulator << std::endl;
            }

            // 1. Update Trailing SL if this tick belongs to our active position
            if (om.isTradeActive && md.token == om.tradeToken) {
                om.updateTrailingSL(md.ltp, jwt, apiKey);
            }

            // 2. Trigger Entry if momentum exceeds threshold (1500 qty imbalance)
            if (!om.isTradeActive && state.ofiAccumulator > Config::OFI_THRESHOLD) {
                if (md.token == Config::CALL_OPTION_TOKEN) {
                    std::cout << "[SIGNAL] Bullish Momentum! Buying Call: " << Config::CALL_OPTION_SYMBOL << std::endl;
                    om.executeScalp(jwt, apiKey, md.token, Config::CALL_OPTION_SYMBOL, Config::LOT_SIZE, md.ltp);
                    state.ofiAccumulator = 0; // Reset after trigger
                } else if (md.token == Config::PUT_OPTION_TOKEN) {
                    std::cout << "[SIGNAL] Bearish Momentum! Buying Put: " << Config::PUT_OPTION_SYMBOL << std::endl;
                    om.executeScalp(jwt, apiKey, md.token, Config::PUT_OPTION_SYMBOL, Config::LOT_SIZE, md.ltp);
                    state.ofiAccumulator = 0; // Reset after trigger
                }
            }
        }
        
        state.lastPrice = md.ltp;
    }
};
#endif