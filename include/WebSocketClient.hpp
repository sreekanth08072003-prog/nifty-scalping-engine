#ifndef WEB_SOCKET_CLIENT_HPP
#define WEB_SOCKET_CLIENT_HPP
#include <string>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <nlohmann/json.hpp>
#include "AngelOneParser.hpp"
#include "StrategyEngine.hpp"
#include "OrderManager.hpp"
#include "Config.hpp"

namespace net = boost::asio;
namespace websocket = boost::beast::websocket;
using tcp = boost::asio::ip::tcp;

class AngelOneWebSocket {
public:
    void connect(const std::string& host, const std::string& jwt, const std::string& api, 
                 const std::string& client, const std::string& feed, class StrategyEngine& se, 
                 class OrderManager& om, const std::atomic<bool>& keepRunning) {
        while (keepRunning) {
            try {
                net::io_context ioc;
                net::ssl::context ctx{net::ssl::context::tlsv12_client};
                tcp::resolver resolver{ioc};
                websocket::stream<net::ssl::stream<tcp::socket>> ws{ioc, ctx};

                auto const results = resolver.resolve(host, "443");
                net::connect(ws.next_layer().next_layer(), results.begin(), results.end());
                
                ws.next_layer().handshake(net::ssl::stream_base::client);

                // Set Request Headers
                ws.set_option(websocket::stream_base::decorator([&](websocket::request_type& req) {
                    req.set("Authorization", jwt);
                    req.set("x-api-key", api);
                    req.set("x-client-code", client);
                    req.set("x-feed-token", feed);
                }));

                ws.handshake(host, "/smart-stream");
                std::cout << "[SYSTEM] Connected to SmartStream. Subscribing..." << std::endl;

                // Send Subscription Packet for Nifty Options defined in Config
                nlohmann::json sub;
                sub["correlationID"] = "abcde12345";
                sub["action"] = 1; // 1 for Subscribe
                sub["params"]["mode"] = 3; // 3 for Full (LTP + Quote)
                sub["params"]["tokenList"] = {
                    {"exchangeType", 2}, // 2 for NFO
                    {"tokens", {Config::CALL_OPTION_TOKEN, Config::PUT_OPTION_TOKEN}}
                };

                ws.binary(true);
                ws.write(net::buffer(sub.dump()));
                
                while (keepRunning && ws.is_open()) {
                    boost::beast::flat_buffer buffer;
                    ws.read(buffer);
                    
                    if (ws.got_binary()) {
                        MarketData md = AngelOneParser::parseFullPacket(buffer.data().data(), buffer.size());
                        if (!md.token.empty()) {
                            se.onTick(md, om, jwt, api);
                        }
                    }
                }
            } catch (std::exception const& e) {
                if (keepRunning) {
                    std::cerr << "[ERROR] WebSocket: " << e.what() << std::endl;
                    std::cout << "[SYSTEM] Connection lost. Retrying in 5 seconds..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            }
        }
    }
};
#endif