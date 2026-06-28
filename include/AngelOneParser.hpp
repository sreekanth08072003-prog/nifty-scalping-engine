#ifndef ANGEL_ONE_PARSER_HPP
#define ANGEL_ONE_PARSER_HPP

#include <cstdint>
#include <cstring>
#include "MarketData.hpp"

#pragma pack(push, 1)
struct SmartStreamPacket {
    uint8_t subscriptionMode;
    uint8_t exchangeType;
    char token[25];
    uint64_t sequenceNumber;
    uint64_t exchangeTimestamp;
    int32_t lastTradedPrice; // Scaled by 100
    int64_t lastTradedQuantity;
    int64_t averageTradedPrice;
    int64_t volume;
    int64_t openInterest;
};
#pragma pack(pop)

class AngelOneParser {
public:
    static MarketData parseFullPacket(const void* data, size_t size) {
        MarketData md;
        md.ltp = 0;
        
        // Smart Stream Packets: 
        // Mode 1 (LTP): 27 bytes
        // Mode 2 (Quote): 46 bytes
        // Mode 3 (SnapQuote): 79+ bytes
        if (size >= 27) { 
            const auto* packet = static_cast<const SmartStreamPacket*>(data);
            md.subscriptionMode = packet->subscriptionMode;
            md.token = std::string(packet->token);
            md.ltp = static_cast<double>(packet->lastTradedPrice) / 100.0;
            
            // Only parse extended fields if the packet size and mode allow it
            if (size >= 79 && md.subscriptionMode == 3) {
                md.volume = packet->volume;
                md.openInterest = packet->openInterest;
                md.lastTradeQty = packet->lastTradedQuantity;
            } else {
                md.volume = 0;
                md.openInterest = 0;
                md.lastTradeQty = 0;
            }
        }
        return md;
    }
};

#endif
