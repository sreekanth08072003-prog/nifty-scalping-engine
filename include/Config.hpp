#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

struct Config {
    static inline std::string CALL_OPTION_TOKEN = "";
    static inline std::string CALL_OPTION_SYMBOL = "";
    static inline std::string PUT_OPTION_TOKEN = "";
    static inline std::string PUT_OPTION_SYMBOL = "";
    static inline int CUTOFF_HOUR = 14;
    static inline int CUTOFF_MINUTE = 30;
    static inline double OFI_THRESHOLD = 1500.0;
    static inline int LOT_SIZE = 50;
};

#endif