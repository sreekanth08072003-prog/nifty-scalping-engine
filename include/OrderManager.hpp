#ifndef ORDER_MANAGER_HPP
#define ORDER_MANAGER_HPP

#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <deque>
#include <thread>
#include <vector>
#include <atomic>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <ctime>
#include "Config.hpp"

using json = nlohmann::json;

class RateLimiter {
private:
    std::deque<std::chrono::steady_clock::time_point> request_timestamps;
    const int MAX_REQUESTS = 9;
    const std::chrono::milliseconds WINDOW = std::chrono::milliseconds(1000);
    std::mutex mtx; // Protects request_timestamps

public:
    void wait() {
        std::lock_guard<std::mutex> lock(mtx);
        auto now = std::chrono::steady_clock::now();
        while (!request_timestamps.empty() && (now - request_timestamps.front() > WINDOW)) {
            request_timestamps.pop_front();
        }
        if (request_timestamps.size() >= MAX_REQUESTS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(112)); // Spread requests
        }
        request_timestamps.push_back(std::chrono::steady_clock::now());
    }
};

class OrderManager {
public:
    OrderManager(const std::string& pubIP, const std::string& locIP) : publicIP(pubIP), localIP(locIP) {}

    enum class TradeStage { NONE, ENTRY_PENDING, EXIT_PENDING };

    struct OrderInfo {
        std::string orderId;
        std::string variety;
    };

    struct OrderResponse {
        bool success;
        std::string orderId;
        std::string message;
    };

    std::atomic<bool> isTradeActive{false};
    std::atomic<TradeStage> currentStage{TradeStage::NONE};
    std::vector<OrderInfo> exitOrders;
    std::mutex ordersMutex;
    RateLimiter limiter;
    std::atomic<bool> stopPolling{false};
    std::thread pollerThread;

    // Helper to check if current time is past the market deadline
    bool isAfterMarketDeadline() {
        std::time_t now = std::time(nullptr);
        std::tm lt;
        if (localtime_r(&now, &lt)) {
            if (lt.tm_hour > Config::CUTOFF_HOUR || (lt.tm_hour == Config::CUTOFF_HOUR && lt.tm_min >= Config::CUTOFF_MINUTE)) return true;
        }
        return false;
    }

    // Helper to log tick-by-tick data for Nifty or active trades
    void logTick(const std::string& symbol, double ltp, int volume) {
        std::string displayName = symbol;
        // Map token to human-readable symbol for better visibility
        if (symbol == Config::CALL_OPTION_TOKEN) displayName = Config::CALL_OPTION_SYMBOL;
        else if (symbol == Config::PUT_OPTION_TOKEN) displayName = Config::PUT_OPTION_SYMBOL;

        std::cout << "[TICK] " << std::left << std::setw(20) << displayName 
                  << " | LTP: " << std::fixed << std::setprecision(2) << std::setw(8) << ltp 
                  << " | Qty: " << volume << std::endl;
    }

    // Helper to format price to 2 decimal places for API compatibility
    std::string formatPrice(double price) {
        // Round to nearest 0.05 (NFO Tick Size)
        double rounded = std::round(price * 20.0) / 20.0;
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << rounded;
        return ss.str();
    }

    // Trade Accounting
    std::string activeEntryId;
    double actualEntryPrice = 0.0;
    int actualQuantityFilled = 0;
    std::string tradeSymbol;
    std::string tradeToken;
    std::chrono::steady_clock::time_point entryPlacementTime;
    double currentStopPrice = 0.0;
    bool isTrailingActive = false;

    // Daily Risk Management
    std::string publicIP;
    std::string localIP;
    double dailyPnL = 0.0;
    const double MAX_LOSS_LIMIT = -2500.0;
    const double MAX_PROFIT_LIMIT = 6500.0; // Suggested profit target
    const std::chrono::minutes POSITION_TIMEOUT = std::chrono::minutes(10);

    std::string sanitize(std::string str) {
        str.erase(str.find_last_not_of(" \n\r\t") + 1);
        str.erase(0, str.find_first_not_of(" \n\r\t"));
        return str;
    }

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    // Private helper to handle all Angel One API communication
    json performApiRequest(const std::string& url, const std::string& jwt, const std::string& key, const json& payload = json::object(), bool isPost = true) {
        limiter.wait();
        std::string readBuffer;
        CURL* curl = curl_easy_init();
        json result = json::object();

        if (curl) {
            struct curl_slist* headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, ("Authorization: Bearer " + sanitize(jwt)).c_str());
            headers = curl_slist_append(headers, ("X-PrivateKey: " + sanitize(key)).c_str());
            headers = curl_slist_append(headers, "X-UserType: USER");
            headers = curl_slist_append(headers, "X-SourceID: WEB");
            headers = curl_slist_append(headers, ("X-ClientLocalIP: " + localIP).c_str());
            headers = curl_slist_append(headers, ("X-ClientPublicIP: " + publicIP).c_str());
            headers = curl_slist_append(headers, "X-MACAddress: 02:00:00:00:00:00");
            headers = curl_slist_append(headers, "Accept: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

            std::string data;
            if (isPost && !payload.empty()) {
                data = payload.dump();
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
            }

            if (curl_easy_perform(curl) == CURLE_OK) {
                try {
                    if (readBuffer.find("<html") == std::string::npos) {
                        result = json::parse(readBuffer);
                    }
                } catch (...) {
                    std::cerr << "[ERROR] JSON Parse Error at " << url << std::endl;
                }
            }
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }
        return result;
    }

    OrderResponse placeOrder(const std::string& jwtToken, const std::string& apiKey,
                            const std::string& symbolToken, const std::string& tradingSymbol,
                            int quantity, double price, const std::string& transactionType,
                            const std::string& variety = "NORMAL",
                            const std::string& orderType = "LIMIT") {
        
        OrderResponse res = {false, "", ""};
        json payload;
        payload["variety"] = variety;
        payload["tradingsymbol"] = tradingSymbol;
        payload["symboltoken"] = symbolToken;
        payload["transactiontype"] = transactionType;
        payload["exchange"] = "NFO";
        payload["ordertype"] = orderType;
        payload["price"] = (orderType == "MARKET") ? "0" : formatPrice(price);
        payload["producttype"] = "CARRYFORWARD";
        payload["duration"] = "DAY";
        payload["quantity"] = std::to_string(quantity);

        auto jRes = performApiRequest("https://apiconnect.angelone.in/rest/secure/angelbroking/order/v1/placeOrder", jwtToken, apiKey, payload);

        if (!jRes.empty() && jRes.contains("data") && !jRes["data"].is_null()) {
            res.success = true;
            res.orderId = jRes["data"]["orderid"];
            std::cout << "[ORDER_SUBMITTED] " << transactionType << " | Symbol: " << tradingSymbol << " | ID: " << res.orderId << std::endl;
        } else if (jRes.contains("message")) {
            res.message = jRes["message"];
            std::cerr << "[ORDER_FAILED] Reason: " << res.message << std::endl;
        }
        return res;
    }

    OrderResponse modifyOrder(const std::string& jwtToken, const std::string& apiKey,
                             const std::string& orderId, int quantity, double price,
                             const std::string& variety) {
        OrderResponse res = {false, "", ""};
        json payload;
        payload["variety"] = variety;
        payload["orderid"] = orderId;
        payload["ordertype"] = variety;
        payload["producttype"] = "CARRYFORWARD";
        payload["duration"] = "DAY";
        payload["price"] = formatPrice(price - 0.05);
        payload["triggerprice"] = formatPrice(price);
        payload["quantity"] = std::to_string(quantity);
        payload["tradingsymbol"] = tradeSymbol;
        payload["symboltoken"] = tradeToken;
        payload["exchange"] = "NFO";

        auto jRes = performApiRequest("https://apiconnect.angelone.in/rest/secure/angelbroking/order/v1/modifyOrder", jwtToken, apiKey, payload);
        if (!jRes.empty() && jRes.contains("status") && jRes["status"].get<bool>()) {
            res.success = true;
        }
        return res;
    }

    void updateTrailingSL(double ltp, const std::string& jwt, const std::string& key) {
        if (currentStage != TradeStage::EXIT_PENDING || exitOrders.empty()) return;

        // Activation: Profit >= 1.0 point
        if (!isTrailingActive) {
            if (ltp >= actualEntryPrice + 1.0) {
                isTrailingActive = true;
                currentStopPrice = actualEntryPrice; // Move SL to Cost initially
                std::cout << "[TRAILING] Activated! SL moved to Cost: " << currentStopPrice << std::endl;
                triggerSLModification(jwt, key);
            }
            return;
        }

        // Trailing logic: Keep SL 1.0 point behind LTP
        double newStop = ltp - 1.0;
        if (newStop > currentStopPrice + 0.25) { // Update in 0.25 increments to save rate limits
            currentStopPrice = newStop;
            std::cout << "[TRAILING] Moving SL up to: " << currentStopPrice << " (LTP: " << ltp << ")" << std::endl;
            triggerSLModification(jwt, key);
        }
    }

    void triggerSLModification(const std::string& jwt, const std::string& key) {
        std::lock_guard<std::mutex> lock(ordersMutex);
        for (auto& order : exitOrders) {
            if (order.variety == "STOPLOSS_LIMIT") {
                modifyOrder(jwt, key, order.orderId, actualQuantityFilled, currentStopPrice, order.variety);
                break;
            }
        }
    }

    double getLtp(const std::string& jwtToken, const std::string& apiKey,
                  const std::string& exchange, const std::string& tradingSymbol,
                  const std::string& symbolToken) {
        double ltp = 0.0;
        json payload;
        payload["exchange"] = exchange;
        payload["tradingsymbol"] = tradingSymbol;
        payload["symboltoken"] = symbolToken;

        auto jRes = performApiRequest("https://apiconnect.angelone.in/order-service/rest/secure/angelbroking/order/v1/getLtpData", jwtToken, apiKey, payload);
        if (!jRes.empty() && jRes.contains("status") && jRes["status"].get<bool>()) {
            ltp = jRes["data"]["ltp"].get<double>();
        }
        return ltp;
    }

    json estimateCharges(const std::string& jwtToken, const std::string& apiKey,
                        const std::string& tradingSymbol, int quantity, double price,
                        const std::string& transactionType) {
        limiter.wait();
        json result;
        CURL* curl = curl_easy_init();
        if (curl) {
            std::string readBuffer;
            
            json order;
            order["exchange"] = "NFO";
            order["tradingsymbol"] = tradingSymbol;
            order["transactiontype"] = transactionType;
            order["quantity"] = quantity;
            order["price"] = formatPrice(price);
            order["producttype"] = "CARRYFORWARD";

            json payload;
            payload["orders"] = json::array({order});
            
            std::string data = payload.dump();
            struct curl_slist* headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, ("Authorization: Bearer " + sanitize(jwtToken)).c_str());
            headers = curl_slist_append(headers, ("X-PrivateKey: " + sanitize(apiKey)).c_str());
            headers = curl_slist_append(headers, "X-UserType: USER");
            headers = curl_slist_append(headers, "X-SourceID: WEB");
            headers = curl_slist_append(headers, ("X-ClientLocalIP: " + localIP).c_str());
            headers = curl_slist_append(headers, ("X-ClientPublicIP: " + publicIP).c_str());
            headers = curl_slist_append(headers, "X-MACAddress: 02:00:00:00:00:00");
            headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
            headers = curl_slist_append(headers, "Accept: application/json");
            headers = curl_slist_append(headers, "Expect:");

            curl_easy_setopt(curl, CURLOPT_URL, "https://apiconnect.angelone.in/rest/secure/angelbroking/brokerage/v1/estimateCharges");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.length());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

            if (curl_easy_perform(curl) == CURLE_OK) {
                long response_code;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                try {
                    if (response_code == 200) {
                        auto jRes = json::parse(readBuffer);
                        if (jRes["status"].get<bool>()) {
                            result = jRes["data"];
                        } else {
                            std::cerr << "[ERROR] estimateCharges failed: " << jRes["message"] << std::endl;
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[ERROR] estimateCharges JSON Parse error: " << e.what() << std::endl;
                }
            }
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }
        return result;
    }

    OrderResponse cancelOrder(const std::string& jwtToken, const std::string& apiKey,
                              const std::string& orderId, const std::string& variety) {
        limiter.wait();
        OrderResponse res = {false, "", ""};
        CURL* curl = curl_easy_init();
        if (curl) {
            std::string readBuffer;
            json payload;
            payload["variety"] = variety;
            payload["orderid"] = orderId;
            std::string data = payload.dump();

            struct curl_slist* headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, ("Authorization: Bearer " + sanitize(jwtToken)).c_str());
            headers = curl_slist_append(headers, ("X-PrivateKey: " + sanitize(apiKey)).c_str());
            headers = curl_slist_append(headers, "X-UserType: USER");
            headers = curl_slist_append(headers, "X-SourceID: WEB");
            headers = curl_slist_append(headers, ("X-ClientLocalIP: " + localIP).c_str());
            headers = curl_slist_append(headers, ("X-ClientPublicIP: " + publicIP).c_str());
            headers = curl_slist_append(headers, "X-MACAddress: 02:00:00:00:00:00");
            headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
            headers = curl_slist_append(headers, "Accept: application/json");
            headers = curl_slist_append(headers, "Expect:");

            curl_easy_setopt(curl, CURLOPT_URL, "https://apiconnect.angelone.in/rest/secure/angelbroking/order/v1/cancelOrder");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.length());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

            if (curl_easy_perform(curl) == CURLE_OK) {
                try {
                    auto jRes = json::parse(readBuffer);
                    if (jRes.contains("status") && jRes["status"].is_boolean() && jRes["status"].get<bool>()) {
                        res.success = true;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[ERROR] cancelOrder Parse Error: " << e.what() << std::endl;
                }
            }
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }
        return res;
    }

    void placeExitOrders(const std::string& jwt, const std::string& key, double entryPrice) {
        std::lock_guard<std::mutex> lock(ordersMutex);
        // 1. Target Order (2 points)
        auto target = placeOrder(jwt, key, tradeToken, tradeSymbol, actualQuantityFilled, entryPrice + 2.0, "SELL");
        if (target.success) exitOrders.push_back({target.orderId, "NORMAL"});

        // 2. Stop Loss Order (e.g., 1.5 points)
        auto sl = placeOrder(jwt, key, tradeToken, tradeSymbol, actualQuantityFilled, entryPrice - 1.5, "SELL", "STOPLOSS_LIMIT");
        if (sl.success) exitOrders.push_back({sl.orderId, "STOPLOSS_LIMIT"});
    }

    void checkTradeStatus(const std::string& jwtToken, const std::string& apiKey) {
        limiter.wait();
        CURL* curl = curl_easy_init();
        if (curl) {
            std::string readBuffer;
            struct curl_slist* headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, ("Authorization: Bearer " + sanitize(jwtToken)).c_str());
            headers = curl_slist_append(headers, ("X-PrivateKey: " + sanitize(apiKey)).c_str());
            headers = curl_slist_append(headers, "X-UserType: USER");
            headers = curl_slist_append(headers, "X-SourceID: WEB");
            headers = curl_slist_append(headers, ("X-ClientLocalIP: " + localIP).c_str());
            headers = curl_slist_append(headers, ("X-ClientPublicIP: " + publicIP).c_str());
            headers = curl_slist_append(headers, "X-MACAddress: 02:00:00:00:00:00");
            headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
            headers = curl_slist_append(headers, "Accept: application/json");
            headers = curl_slist_append(headers, "Expect:");

            curl_easy_setopt(curl, CURLOPT_URL, "https://apiconnect.angelone.in/rest/secure/angelbroking/order/v1/getOrderBook");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

            if (curl_easy_perform(curl) == CURLE_OK) {
                try {
                    auto jRes = json::parse(readBuffer);

                    if (jRes["status"].get<bool>() && jRes.contains("data") && jRes["data"].is_array()) {
                        // 1. Update Fill status and Transition Stage
                        for (const auto& ao : jRes["data"]) {
                            if (ao["orderid"] == activeEntryId) {
                                actualQuantityFilled = std::stoi(ao["filledshares"].get<std::string>());
                                
                                if (currentStage == TradeStage::ENTRY_PENDING && ao["status"] == "complete") {
                                    actualEntryPrice = std::stod(ao["averageprice"].get<std::string>());
                                    std::cout << "[TRADE_LOG][ENTRY_FILLED] Symbol: " << tradeSymbol << " | Price: " << actualEntryPrice << " | Qty: " << actualQuantityFilled << std::endl;
                                    currentStage = TradeStage::EXIT_PENDING;
                                    placeExitOrders(jwtToken, apiKey, actualEntryPrice);
                                } else if (currentStage == TradeStage::ENTRY_PENDING && (ao["status"] == "rejected" || ao["status"] == "cancelled")) {
                                    std::cerr << "[TRADE_LOG][ORDER_FAILED] Type: ENTRY | Status: " << ao["status"].get<std::string>() << " | Reason: " << (ao.contains("text") ? ao["text"].get<std::string>() : "Unknown") << std::endl;
                                    isTradeActive = false;
                                    currentStage = TradeStage::NONE;
                                }
                                break;
                            }
                        }

                        // 2. Check for Position Timeout (5 mins) or Market Deadline (14:30)
                        auto now = std::chrono::steady_clock::now();
                        bool isTimedOut = (now - entryPlacementTime >= POSITION_TIMEOUT);
                        if (isTradeActive && (isTimedOut || isAfterMarketDeadline())) {
                            if (isTimedOut) std::cout << "[TIMEOUT] 10-minute limit reached. Squaring off..." << std::endl;
                            else std::cout << "[DEADLINE] Past 14:30 IST. Emergency Square-off..." << std::endl;
                            
                            // Stop further entry fills
                            if (currentStage == TradeStage::ENTRY_PENDING) {
                                cancelOrder(jwtToken, apiKey, activeEntryId, "NORMAL");
                            }

                            // Cancel existing Target/SL orders
                            std::vector<OrderInfo> toCancel;
                            {
                                std::lock_guard<std::mutex> lock(ordersMutex);
                                toCancel = exitOrders;
                                exitOrders.clear();
                            }
                            for (const auto& target : toCancel) {
                                cancelOrder(jwtToken, apiKey, target.orderId, target.variety);
                            }

                            // Exit whatever quantity we currently hold
                            if (actualQuantityFilled > 0) {
                                placeOrder(jwtToken, apiKey, tradeToken, tradeSymbol, actualQuantityFilled, 0, "SELL", "NORMAL", "MARKET");
                            }
                            
                            isTradeActive = false;
                            currentStage = TradeStage::NONE;
                            return;
                        }

                        // 3. Check for Entry Timeout (30s)
                        if (currentStage == TradeStage::ENTRY_PENDING) {
                            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - entryPlacementTime).count();
                            if (elapsed >= 30) {
                                std::cout << "[TIMEOUT] Entry not filled in 30s. Cancelling..." << std::endl;
                                cancelOrder(jwtToken, apiKey, activeEntryId, "NORMAL");
                                if (actualQuantityFilled > 0) {
                                    placeOrder(jwtToken, apiKey, tradeToken, tradeSymbol, actualQuantityFilled, 0, "SELL", "NORMAL", "MARKET");
                                }
                                isTradeActive = false;
                                currentStage = TradeStage::NONE;
                                return;
                            }
                        }

                        // 4. OCO (One Cancels the Other) Logic for Target/SL
                        std::vector<OrderInfo> toCancel;
                        {
                            std::lock_guard<std::mutex> lock(ordersMutex);
                            bool anyFilled = false;
                            for (const auto& eo : exitOrders) {
                                for (const auto& ao : jRes["data"]) {
                                    if (ao["orderid"] == eo.orderId && ao["status"] == "complete") {
                                        double exitPrice = std::stod(ao["averageprice"].get<std::string>());
                                        std::cout << "[TRADE_LOG][EXIT_FILLED] Symbol: " << tradeSymbol << " | Price: " << exitPrice << " | Qty: " << ao["filledshares"] << std::endl;
                                        double tradePnL = (exitPrice - actualEntryPrice) * actualQuantityFilled;
                                        dailyPnL += tradePnL;
                                        
                                        std::cout << "[TRADE_LOG][EXIT_FILLED] Symbol: " << tradeSymbol << " | Price: " << exitPrice << " | PnL: " << tradePnL << " | Total: " << dailyPnL << std::endl;
                                        
                                        anyFilled = true;
                                        break;
                                    }
                                }
                                if (anyFilled) break;
                            }

                            bool allClosed = true;
                            for (const auto& eo : exitOrders) {
                                bool foundOpen = false;
                                for (const auto& ao : jRes["data"]) {
                                    if (ao["orderid"] == eo.orderId) {
                                        std::string s = ao["status"];
                                        if (s != "complete" && s != "cancelled" && s != "rejected") {
                                            foundOpen = true;
                                            if (anyFilled) toCancel.push_back(eo);
                                        }
                                        break;
                                    }
                                }
                                if (foundOpen) allClosed = false;
                            }

                            if (allClosed && !exitOrders.empty()) {
                                std::cout << "[POLLER] Position closed. Resetting isTradeActive." << std::endl;
                                isTradeActive = false;
                                exitOrders.clear();
                            }
                        }
                        for (const auto& target : toCancel) {
                            std::cout << "[POLLER] OCO: Cancelling order " << target.orderId << std::endl;
                            cancelOrder(jwtToken, apiKey, target.orderId, target.variety);
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[ERROR] checkTradeStatus Logic Error: " << e.what() << std::endl;
                }
            }
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }
    }

    ~OrderManager() {
        stopPolling = true;
        if (pollerThread.joinable()) {
            pollerThread.join();
        }
    }

    void startOrderBookPolling(const std::string& jwt, const std::string& key) {
        if (pollerThread.joinable()) {
            return; // Already running
        }
        stopPolling = false;
        pollerThread = std::thread([this, jwt, key]() {
            while (!this->stopPolling) {
                if (this->isTradeActive) {
                    this->checkTradeStatus(jwt, key);
                }
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        });
    }

    // logic for 2-point scalp execution
    void executeScalp(const std::string& jwt, const std::string& key, const std::string& token, const std::string& symbol, int quantity, double entryPrice) {
        if (isTradeActive) return;

        // Block entry after 14:30 IST
        if (isAfterMarketDeadline()) {
            std::cout << "[BLOCKED] Entry denied: Past " << Config::CUTOFF_HOUR << ":" << Config::CUTOFF_MINUTE << " deadline." << std::endl;
            return;
        }

        // Check Risk Limits before entering
        if (dailyPnL <= MAX_LOSS_LIMIT) {
            std::cout << "[BLOCKED] Entry denied: Max daily loss of " << MAX_LOSS_LIMIT << " reached." << std::endl;
            return;
        }
        if (dailyPnL >= MAX_PROFIT_LIMIT) {
            std::cout << "[BLOCKED] Entry denied: Daily profit target of " << MAX_PROFIT_LIMIT << " reached." << std::endl;
            return;
        }
        
        isTradeActive = true;
        currentStage = TradeStage::ENTRY_PENDING;
        tradeSymbol = symbol;
        tradeToken = token;
        entryPlacementTime = std::chrono::steady_clock::now();

        auto entry = placeOrder(jwt, key, token, symbol, quantity, entryPrice, "BUY");
        if(entry.success) {
            activeEntryId = entry.orderId;
            std::cout << "[SCALP] Entry Submitted. Waiting for full fill..." << std::endl;
        } else {
            isTradeActive = false;
            currentStage = TradeStage::NONE;
        }
    }
};

#endif
