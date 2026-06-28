#ifndef WEBSOCKET_CLIENT_HPP
#define WEBSOCKET_CLIENT_HPP

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <iostream>
#include "AngelOneParser.hpp"
#include "Config.hpp"
#include "StrategyEngine.hpp"
#include "OrderManager.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

class AngelOneWebSocket {
public:
    void connect(const std::string& host,
             const std::string& jwtToken,
             const std::string& apiKey,
             const std::string& clientCode,
             const std::string& feedToken,
             StrategyEngine& engine,
             OrderManager& om,
             const std::string& path = "/smart-stream") {
    try {
        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        tcp::resolver resolver{ioc};
        websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws{ioc, ctx};

        std::string port = "443";

        std::cout << "[DEBUG] Resolving " << host << "..." << std::endl;
        auto const results = resolver.resolve(host, port);

        std::cout << "[DEBUG] Connecting to IP..." << std::endl;
        beast::get_lowest_layer(ws).connect(results);

        std::cout << "[DEBUG] SSL Handshake..." << std::endl;
        ws.next_layer().handshake(ssl::stream_base::client);

        ws.set_option(websocket::stream_base::decorator(
            [&](websocket::request_type& req) {
                req.set("Authorization", "Bearer " + jwtToken);
                req.set("x-api-key", apiKey);
                req.set("x-client-code", clientCode);
                req.set("x-feed-token", feedToken);
                req.set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
            }));

        std::cout << "[DEBUG] WebSocket Handshake..." << std::endl;
        // Using the query-parameter format recommended for browser-based clients
        std::string fullPath = path + "?clientCode=" + clientCode + "&feedToken=" + feedToken + "&apiKey=" + apiKey;
        ws.handshake(host, fullPath);

        // 1. Update Subscription: Subscribe to both Spot (for analysis) and Options (for execution/trailing)
        // exchangeType 1 = NSE (Spot), 2 = NFO (Options)
        std::string subRequest =
            "{\"action\":1,\"params\":{\"mode\":3,\"tokenList\":["
            "{\"exchangeType\":1,\"tokens\":[\"" + Config::INDEX_TOKEN + "\"]},"
            "{\"exchangeType\":2,\"tokens\":[\"" + Config::OPTION_TOKEN + "\"]}]}}";

        ws.write(net::buffer(subRequest));

        std::cout << "--- CONNECTED TO LIVE STREAM ---" << std::endl;

        for (;;) {
            beast::flat_buffer buffer;
            ws.read(buffer);

            MarketData md = AngelOneParser::parseFullPacket(
                buffer.data().data(), buffer.size());

            if (md.ltp > 0) {
                std::cout << "[TICK] Token: " << md.token 
                          << " | LTP: " << md.ltp 
                          << " | Vol: " << md.volume 
                          << " | OI: " << md.openInterest << std::endl;

                // Defensive Check: Don't process signals if volume is 0 (Market Closed/LTP mode)
                if (md.volume <= 0) {
                    continue; 
                }

                // Handle Trailing Stop Loss if a trade is active for this token
                if (om.isTradeActive && md.token == om.tradeToken) {
                    om.updateTrailingSL(md.ltp, jwtToken, apiKey);
                }

                // Process signal
                auto signal = engine.analyzeOrderFlow(md);
                
                if (signal.buyCall && !om.isTradeActive) {
                    std::cout << "[SIGNAL] Bullish OFI detected. Confidence: " << signal.confidence << std::endl;
                    
                    om.executeScalp(jwtToken, apiKey, Config::OPTION_TOKEN, Config::OPTION_SYMBOL, Config::TOTAL_QUANTITY);
                }
            }
        }

    } catch (std::exception const& e) {
        std::cerr << "[ERROR] WebSocket: " << e.what() << std::endl;
    }
}
};
#endif
