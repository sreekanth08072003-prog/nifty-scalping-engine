#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

namespace Config {
    // --- ORDER SETTINGS ---
    // Enter the total quantity (units) here. 
    // For Nifty: 65 = 1 lot, 130 = 2 lots, etc.
    inline int TOTAL_QUANTITY = 1300; 

    // --- TARGET CONTRACT ---
    inline std::string OPTION_TOKEN = "152431";
    inline std::string OPTION_SYMBOL = "NIFTY25JAN2422000CE";

    // --- ANALYSIS SETTINGS ---
    inline std::string INDEX_TOKEN = "3045"; // Nifty 50 Spot

    // --- TIME LIMITS ---
    inline int CUTOFF_HOUR = 14;   // 14:00 (IST)
    inline int CUTOFF_MINUTE = 30; // 14:30 (IST)
}

#endif