#include <iostream>
#include "AngelOneLogin.hpp"
#include "WebSocketClient.hpp"
#include "StrategyEngine.hpp"
#include "OrderManager.hpp"
#include <curl/curl.h>
#include <cstdlib>

std::string getMyIP() {
    CURL* curl = curl_easy_init();
    std::string res = "0.0.0.0";
    if (curl) {
        std::string buffer;
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.ipify.org");
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void* contents, size_t size, size_t nmemb, void* userp) {
            ((std::string*)userp)->append((char*)contents, size * nmemb);
            return size * nmemb;
        });
        if (curl_easy_perform(curl) == CURLE_OK) res = buffer;
        curl_easy_cleanup(curl);
    }
    return res;
}

std::string getLocalIP() {
    char buffer[128];
    std::string result = "127.0.0.1";
    FILE* pipe = popen("hostname -I | awk '{print $1}'", "r");
    if (pipe) {
        if (fgets(buffer, sizeof(buffer), pipe) != NULL) result = std::string(buffer);
        pclose(pipe);
    }
    result.erase(result.find_last_not_of(" \n\r\t") + 1);
    return result;
}

int main() {
    // Load Credentials from Environment Variables for Security
    const char* env_client_id = std::getenv("ANGEL_CLIENT_ID");
    const char* env_password  = std::getenv("ANGEL_PASSWORD");
    const char* env_api_key   = std::getenv("ANGEL_API_KEY");
    const char* env_totp_seed = std::getenv("ANGEL_TOTP_SEED");

    if (!env_client_id || !env_password || !env_api_key || !env_totp_seed) {
        std::cerr << "[ERROR] Missing credentials! Please set ANGEL_CLIENT_ID, ANGEL_PASSWORD, ANGEL_API_KEY, and ANGEL_TOTP_SEED environment variables." << std::endl;
        return 1;
    }

    std::string client_id = env_client_id;
    std::string password  = env_password;
    std::string api_key   = env_api_key;
    std::string seed      = env_totp_seed;

    std::cout << "--- STARTING AUTO-TRADING ENGINE ---" << std::endl;
    std::string currentIP = getMyIP();
    std::string localIP = getLocalIP();
    std::cout << "[INFO] Outgoing IP: " << currentIP << std::endl;
    std::cout << "[INFO] Local IP: " << localIP << std::endl;

    AngelOneLogin auth(currentIP, localIP);
    auto session = auth.login(client_id, password, seed, api_key);

    if (session.success) {
        std::cout << "Login Successful! Starting Live Stream..." << std::endl;
        std::cout << "[DEBUG] JWT Token Length: " << session.jwtToken.length() << std::endl;

        StrategyEngine engine;
        OrderManager om(currentIP, localIP);

        // Fetch Nifty 50 Spot Price
        double niftySpot = om.getLtp(session.jwtToken, api_key, "NSE", "Nifty 50", "99926000");
        std::cout << "[MARKET] Current Nifty 50 Spot: " << niftySpot << std::endl;

        if (niftySpot <= 0) {
            std::cerr << "[WARNING] Market seems closed or Token is invalid. Delta calculations will be skipped." << std::endl;
        }

        // Use niftySpot as 'S' in your Delta calculations to find OTM strikes.
        om.startOrderBookPolling(session.jwtToken, api_key);

        AngelOneWebSocket ws;
        // Host for Market Data: smartapisocket.angelone.in
        // Host for Order Updates: tns.angelone.in (path: /smart-order-update)
        ws.connect("smartapisocket.angelone.in", session.jwtToken, api_key, client_id, session.feedToken, engine, om);

    } else {
        std::cerr << "Login Failed. Check credentials and TOTP Seed." << std::endl;
    }

    return 0;
}
