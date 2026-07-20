#include <iostream>
#include <cstdlib>
#include <fstream>
#include <csignal>
#include <atomic>
#include <thread>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "AngelOneLogin.hpp"
#include "OrderManager.hpp"
#include "StrategyEngine.hpp"
#include "WebSocketClient.hpp"
#include "InstrumentFilter.hpp"

namespace {
    std::atomic<bool> keepRunning{true};
    void signalHandler(int signum) {
        std::cout << "\n[SYSTEM] Termination signal (" << signum << ") received. Shutting down..." << std::endl;
        keepRunning = false;
    }
}

// Helper to fetch the master contract from Angel One
nlohmann::json downloadUrl(const std::string& url, std::string& readBuffer) {
    readBuffer.clear();
    CURL* curl = curl_easy_init();
    CURLcode res;
    long response_code = 0;

    if (curl) {
        std::cout << "[INFO] Attempting download from: " << url << std::endl;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
        
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
            size_t totalSize = size * nmemb;
            static_cast<std::string*>(userp)->append(static_cast<char*>(contents), totalSize);
            return totalSize;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L); 
        
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            if (response_code != 200) {
                std::cerr << "[WARNING] Server returned HTTP " << response_code << " for " << url << std::endl;
            }
        } else {
            std::cerr << "[ERROR] Curl failed for " << url << ": " << curl_easy_strerror(res) << std::endl;
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (response_code == 200 && !readBuffer.empty()) {
            try {
                return nlohmann::json::parse(readBuffer);
            } catch (const nlohmann::json::parse_error& e) {
                std::cerr << "[ERROR] JSON Parse Error: " << e.what() << std::endl;
                return nlohmann::json::object();
            } catch (...) {
                return nlohmann::json::object();
            }
        }
    }
    return nlohmann::json::object();
}

nlohmann::json fetchMasterContract() {
    std::string cachePath = "instruments.json";
    const char* envUrl = std::getenv("ANGEL_MASTER_URL");
    
    // Try local cache first
    std::ifstream cacheIn(cachePath);
    if (cacheIn.is_open()) {
        std::cout << "[INFO] Loading Master Contract from local cache..." << std::endl;
        try {
            nlohmann::json j;
            cacheIn >> j;
            return j;
        } catch (...) {
            std::cerr << "[WARNING] Local cache corrupted, downloading fresh copy..." << std::endl;
        }
    }

    std::string readBuffer;
    nlohmann::json result;

    // Priority 1: Environment Variable
    if (envUrl) {
        result = downloadUrl(envUrl, readBuffer);
    }

    // Priority 2: Verified Standard URL (Correct Spelling)
    if (result.empty()) {
        result = downloadUrl("https://margincalculator.angelbroking.com/OpenAPI_Standard/ms/opentoken/AllFormatted.json", readBuffer);
    }

    // Priority 3: New AngelOne Domain Fallback
    if (result.empty()) {
        result = downloadUrl("https://margincalculator.angelone.in/OpenAPI_Standard/ms/opentoken/AllFormatted.json", readBuffer);
    }

    // Priority 3: Legacy URL (Fallback)
    if (result.empty()) {
        result = downloadUrl("https://margincalculator.angelbroking.com/OpenApi_0.1/static/allExchangeData.json", readBuffer);
    }
    
    if (result.empty()) {
        std::cerr << "[CRITICAL] All Master Contract download attempts failed. Check internet connection or Angel One API status." << std::endl;
        return nlohmann::json::object();
    }

    if (!result.empty()) {
        std::ofstream cacheOut(cachePath);
        if (cacheOut.is_open()) {
            cacheOut << result.dump();
            std::cout << "[INFO] Master Contract cached to " << cachePath << std::endl;
        }
    }

    return result;
}

int main() {
    std::cout << "[SYSTEM] Angel One Trading Engine v1.0 starting..." << std::endl;

    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // 1. Load Environment Variables
    const char* clientId = std::getenv("ANGEL_CLIENT_ID");
    const char* password = std::getenv("ANGEL_PASSWORD");
    const char* seed = std::getenv("ANGEL_TOTP_SEED");
    const char* apiKey = std::getenv("ANGEL_API_KEY");
    const char* localIp = std::getenv("LOCAL_IP");
    const char* publicIp = std::getenv("PUBLIC_IP");

    if (!clientId || !password || !seed || !apiKey) {
        if (!clientId) std::cerr << "[ERROR] Missing environment variable: ANGEL_CLIENT_ID" << std::endl;
        if (!password) std::cerr << "[ERROR] Missing environment variable: ANGEL_PASSWORD" << std::endl;
        if (!seed)     std::cerr << "[ERROR] Missing environment variable: ANGEL_TOTP_SEED" << std::endl;
        if (!apiKey)   std::cerr << "[ERROR] Missing environment variable: ANGEL_API_KEY" << std::endl;
        std::cerr << "[CRITICAL] Configuration incomplete. Application exiting." << std::endl;
        return 1;
    }

    // 1.1 Log configuration for debugging (masking sensitive data)
    std::cout << "[INFO] Configuration loaded for Client: " << clientId << std::endl;
    std::cout << "[INFO] Using API Key: " << std::string(apiKey).substr(0, 4) << "...." << std::endl;

    std::string locIP = localIp ? localIp : "127.0.0.1";
    std::string pubIP = publicIp ? publicIp : "127.0.0.1";

    // 2. Initialize Components
    AngelOneLogin loginHandler(pubIP, locIP);
    OrderManager orderMgr(pubIP, locIP);
    StrategyEngine strategy;
    AngelOneWebSocket wsClient;

    // 3. Authenticate
    auto session = loginHandler.login(clientId, password, seed, apiKey);
    if (!session.success) {
        std::cerr << "[FATAL] Login failed. Check credentials and TOTP seed." << std::endl;
        return 1;
    }
    std::cout << "[SUCCESS] Authenticated. JWT Session acquired." << std::endl;

    // 4. Resolve Instruments (Mocking master contract fetch for logic flow)
    // In a production app, you would fetch the full JSON from Angel One's OpenApi
    std::cout << "[INFO] Fetching Nifty Weekly Options..." << std::endl;
    
    nlohmann::json masterContract = fetchMasterContract();
    
    if (masterContract.empty()) {
        std::cerr << "[ERROR] Failed to download or parse master contract. Exiting." << std::endl;
        return 1;
    }

    // Note: In real usage, you'd fetch the Nifty Spot LTP first
    double niftySpotLtp = orderMgr.getLtp(session.jwtToken, apiKey, "NSE", "Nifty 50", "99926000");
    
    // Handle Closed Market for Testing
    if (niftySpotLtp <= 0) {
        const char* mockSpot = std::getenv("MOCK_SPOT");
        if (mockSpot) {
            niftySpotLtp = std::stod(mockSpot);
            std::cout << "[TEST] Market appears closed. Using MOCK_SPOT: " << niftySpotLtp << std::endl;
        } else {
            std::cerr << "[WARNING] Market is closed and MOCK_SPOT is not set. Instrument discovery will fail." << std::endl;
        }
    }

    if (niftySpotLtp > 0) {
        std::cout << "[MARKET] Nifty Spot LTP: " << niftySpotLtp << std::endl;
        // Ensure mockMaster is populated with Angel One's OpenApi JSON to resolve tokens
        InstrumentFilter::findWeeklyScalpInstruments(masterContract, niftySpotLtp, orderMgr, session.jwtToken, apiKey);
    } else {
        std::cerr << "[ERROR] Cannot proceed without a valid Spot price. Set MOCK_SPOT for offline testing." << std::endl;
    }

    // 5. Start Background Threads
    orderMgr.startOrderBookPolling(session.jwtToken, apiKey);

    // --- COMPREHENSIVE TESTING BLOCK ---
    const char* simulate = std::getenv("SIMULATE_STRATEGY");
    if (simulate && std::string(simulate) == "true") {
        std::cout << "\n========== STARTING OFFLINE TEST SUITE ==========" << std::endl;
        
        // Scenario 1: Bullish Trend Simulation
        std::cout << "[TEST] Scenario 1: Simulating Bullish Trend (Expecting Buy Signal)..." << std::endl;
        std::vector<std::pair<double, long>> mockTicks = {
            {24334.3, 1000}, {24335.0, 1200}, {24336.5, 2500}, 
            {24338.0, 5000}, {24340.0, 8000}, {24342.5, 12000}
        };

        for (const auto& tick : mockTicks) {
            MarketData md;
            // Use the identified token if available, otherwise fallback to mock
            md.token = Config::CALL_OPTION_TOKEN.empty() ? "3045" : Config::CALL_OPTION_TOKEN;
            md.ltp = tick.first;
            md.lastTradeQty = static_cast<double>(tick.second);

            // Use the unified onTick method for state updates and signal logic
            strategy.onTick(md, orderMgr, session.jwtToken, apiKey);
        }
        
        std::cout << "[TEST] Final PnL Check: " << orderMgr.dailyPnL << std::endl;
        std::cout << "========== OFFLINE TEST SUITE COMPLETE ==========\n" << std::endl;
        
        // If simulating, you might want to exit after the test
        const char* exitAfter = std::getenv("EXIT_AFTER_TEST");
        if (exitAfter && std::string(exitAfter) == "true") return 0;
    }
    // ------------------------------

    // 6. Connect to Real-time Stream in a background thread
    std::cout << "[SYSTEM] Starting WebSocket Stream..." << std::endl;
    
    std::thread wsThread([&]() {
        wsClient.connect("smartapisocket.angelone.in", 
                         session.jwtToken, 
                         apiKey, 
                         clientId, 
                         session.feedToken, 
                         strategy, 
                         orderMgr,
                         keepRunning);
    });

    // Now the main thread is free to handle signals and monitor the app
    std::cout << "[INFO] Stream active. Press Ctrl+C to terminate." << std::endl;
    
    while(keepRunning) {
        if (orderMgr.isAfterMarketDeadline()) {
            std::cout << "[SYSTEM] Market cutoff time reached (14:30 IST). Initiating shutdown..." << std::endl;
            keepRunning = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); 
    }

    std::cout << "[SYSTEM] Cleaning up and exiting..." << std::endl;
    
    if (wsThread.joinable()) {
        // Wait for the WebSocket loop (which checks keepRunning) to finish
        wsThread.join(); 
    }

    return 0;
}