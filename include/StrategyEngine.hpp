#ifndef STRATEGY_ENGINE_HPP
#define STRATEGY_ENGINE_HPP
#include "MarketData.hpp"
#include <cmath>
#include <vector>
#include <deque>
#include <numeric>

class StrategyEngine {
private:
    std::deque<long long> volumeWindow;
    const size_t WINDOW_SIZE = 20; // Last 20 ticks
    double prevLtp = 0.0;

public:
    struct ScalpSignal {
        bool buyCall = false;
        bool buyPut = false;
        double confidence = 0.0;
    };

    // Advanced Math: Z-Score of Order Flow Imbalance
    ScalpSignal analyzeOrderFlow(const MarketData& md) {
        ScalpSignal signal;
        
        // 1. Calculate Imbalance (Aggressor Volume)
        long long currentImbalance = 0;
        if (md.ltp > prevLtp) currentImbalance = md.lastTradeQty;
        else if (md.ltp < prevLtp) currentImbalance = -md.lastTradeQty;
        
        prevLtp = md.ltp;
        volumeWindow.push_back(currentImbalance);
        if (volumeWindow.size() > WINDOW_SIZE) volumeWindow.pop_front();

        if (volumeWindow.size() < WINDOW_SIZE) return signal;

        // 2. Statistical Metrics
        double sum = std::accumulate(volumeWindow.begin(), volumeWindow.end(), 0.0);
        double mean = sum / WINDOW_SIZE;
        double sq_sum = std::inner_product(volumeWindow.begin(), volumeWindow.end(), volumeWindow.begin(), 0.0);
        double stdev = std::sqrt(sq_sum / WINDOW_SIZE - mean * mean);

        // 3. Z-Score Calculation
        double zScore = (currentImbalance - mean) / (stdev + 0.0001);
        signal.confidence = std::abs(zScore);

        // Trigger if momentum is 2 standard deviations away from mean
        if (zScore > 2.0) signal.buyCall = true;
        else if (zScore < -2.0) signal.buyPut = true;

        return signal;
    }

    // Delta Calculation to find "Cheap Options" (Target Delta ~ 0.1)
    double calculateDelta(double S, double K, double T, double r, double sigma, bool isCall) {
        if (S <= 0 || K <= 0 || sigma <= 0 || T <= 0) {
            return 0.0;
        }

        double d1 = (std::log(S / K) + (r + (sigma * sigma) / 2.0) * T) / (sigma * std::sqrt(T));
        if (isCall) {
            return 0.5 * std::erfc(-d1 * 0.70710678118);
        } else {
            return (0.5 * std::erfc(-d1 * 0.70710678118)) - 1.0;
        }
    }

    bool isIdealScalpingDelta(double delta) {
        // Target options moving 1 point for every 10 point Nifty move (Delta 0.1)
        return std::abs(delta) >= 0.08 && std::abs(delta) <= 0.12;
    }

    double solveIV(double target, double S, double K, double T, double r) {
        double sigma = 0.5; // Initial guess
        for (int i = 0; i < 10; ++i) {
            double d1 = (std::log(S / K) + (r + (sigma * sigma) / 2.0) * T) / (sigma * std::sqrt(T));
            double d2 = d1 - sigma * std::sqrt(T);
            
            // Black-Scholes Call Price
            double price = S * (0.5 * std::erfc(-d1 * 0.70710678118)) - K * std::exp(-r * T) * (0.5 * std::erfc(-d2 * 0.70710678118));
            
            double diff = target - price;
            if (std::abs(diff) < 0.01) return sigma;
            
            // Vega calculation
            double v = S * std::sqrt(T) * (std::exp(-d1 * d1 / 2.0) / 2.50662827463);
            sigma += diff / (v + 0.0001); // Update sigma
        }
        return sigma;
    }
};
#endif
