#ifndef INSTRUMENT_FILTER_HPP
#define INSTRUMENT_FILTER_HPP

#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include "Config.hpp"

struct OptionCandidate {
    std::string symbol;
    std::string token;
    double strike;
    std::string expiry;
    std::string type; // "CE" or "PE"
};

class InstrumentFilter {
public:
    // Helper to convert AngelOne expiry string (e.g., "31OCT2024") to sortable YYYYMMDD
    static std::string toSortableDate(std::string expiry) {
        static std::map<std::string, std::string> months = {
            {"JAN","01"},{"FEB","02"},{"MAR","03"},{"APR","04"},{"MAY","05"},{"JUN","06"},
            {"JUL","07"},{"AUG","08"},{"SEP","09"},{"OCT","10"},{"NOV","11"},{"DEC","12"}
        };
        if (expiry.length() < 7) return "99999999";
        std::string day = expiry.substr(0, 2);
        std::string mon = expiry.substr(2, 3);
        std::string year = (expiry.length() >= 9) ? expiry.substr(5, 4) : "20" + expiry.substr(5, 2);
        
        if (months.find(mon) == months.end()) return "99999999";
        return year + months[mon] + day;
    }

    // Using a template for OrderManager to avoid circular dependency
    template<typename T>
    static bool findWeeklyScalpInstruments(const nlohmann::json& master, 
                                         double currentSpot,
                                         T& om,
                                         const std::string& jwt,
                                         const std::string& apiKey,
                                         bool ignorePriceRange = false) {
        
        std::vector<OptionCandidate> allOptions;
        std::vector<std::string> expiries;

        // 1. Filter Nifty Options and find unique expiries
        for (const auto& item : master) {
            if (item.contains("name") && item["name"] == "NIFTY" &&
                item.contains("exch_seg") && item["exch_seg"] == "NFO" &&
                item.contains("instrumenttype") && item["instrumenttype"] == "OPTIDX") {
                
                OptionCandidate oc;
                oc.symbol = item["symbol"];
                oc.token = item["token"];
                oc.strike = std::stod(item["strike"].get<std::string>()) / 100.0;
                oc.expiry = item["expiry"];
                oc.type = (oc.symbol.find("CE") != std::string::npos) ? "CE" : "PE";
                
                allOptions.push_back(oc);
                expiries.push_back(oc.expiry);
            }
        }

        if (expiries.empty()) return false;

        // 2. Identify unique expiries and sort to find "Next Week"
        std::sort(expiries.begin(), expiries.end(), [](const std::string& a, const std::string& b) {
            return toSortableDate(a) < toSortableDate(b);
        });
        expiries.erase(std::unique(expiries.begin(), expiries.end()), expiries.end());
        
        // Select Index 1 for Next Week, fallback to Index 0 if only one expiry exists
        std::string weeklyExpiry = (expiries.size() > 1) ? expiries[1] : expiries[0];
        std::cout << "[FILTER] Identified Next Week Expiry: " << weeklyExpiry << std::endl;

        // 3. Filter for candidates near ATM to check LTP (Range 18-24)
        auto filterSide = [&](const std::string& type) -> bool {
            std::vector<OptionCandidate> candidates;
            for (auto& opt : allOptions) {
                if (opt.expiry == weeklyExpiry && opt.type == type) {
                    // Only check strikes within +/- 500 points of spot to minimize API calls
                    if (std::abs(opt.strike - currentSpot) < 500) {
                        candidates.push_back(opt);
                    }
                }
            }

            // Sort by strike to check systematically
            std::sort(candidates.begin(), candidates.end(), [](const OptionCandidate& a, const OptionCandidate& b) {
                return a.strike < b.strike;
            });

            for (auto& cand : candidates) {
                double ltp = 0.0;
                if (!ignorePriceRange) {
                    // Small sleep to avoid hitting Angel One Rate Limits (3 per sec for some endpoints)
                    std::this_thread::sleep_for(std::chrono::milliseconds(334));
                    ltp = om.getLtp(jwt, apiKey, "NFO", cand.symbol, cand.token);
                }

                if (ignorePriceRange || (ltp >= 18.0 && ltp <= 24.0)) {
                    if (type == "CE") {
                        Config::CALL_OPTION_TOKEN = cand.token;
                        Config::CALL_OPTION_SYMBOL = cand.symbol;
                    } else {
                        Config::PUT_OPTION_TOKEN = cand.token;
                        Config::PUT_OPTION_SYMBOL = cand.symbol;
                    }
                    std::cout << "[FILTER] Found " << type << (ignorePriceRange ? " (Mock Mode)" : " within range 18-24") 
                              << ": " << cand.symbol << (ignorePriceRange ? "" : " at LTP: " + std::to_string(ltp)) << std::endl;
                    return true;
                }
            }
            return false;
        };

        return filterSide("CE") && filterSide("PE");
    }
};

#endif