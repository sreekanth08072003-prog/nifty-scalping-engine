#ifndef ANGEL_ONE_PARSER_HPP
#define ANGEL_ONE_PARSER_HPP
#include "MarketData.hpp"
#include <vector>
#include <cstring>
#include <cstdint>

class AngelOneParser {
public:
    static MarketData parseFullPacket(const void* data, size_t size) {
        MarketData md;
        if (size < 43) return md; // Basic size check for Angel binary packet

        const uint8_t* buf = static_cast<const uint8_t*>(data);
        
        // Angel One Binary Protocol offsets (Simplified for this example)
        char tokenBuf[26];
        std::memcpy(tokenBuf, buf + 2, 25);
        tokenBuf[25] = '\0'; // Ensure null termination
        md.token = std::string(tokenBuf);

        // Extract LTP (Offset 28, 4 bytes, Little Endian, value in paisa)
        int32_t ltp_paisa;
        std::memcpy(&ltp_paisa, buf + 28, 4);
        md.ltp = static_cast<double>(ltp_paisa) / 100.0;

        // Extract Last Trade Qty (Offset 32, 4 bytes)
        int32_t qty;
        std::memcpy(&qty, buf + 32, 4);
        md.lastTradeQty = static_cast<double>(qty);

        return md;
    }
};

#endif