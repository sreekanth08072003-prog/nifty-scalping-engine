#ifndef ANGEL_ONE_LOGIN_HPP
#define ANGEL_ONE_LOGIN_HPP

#include <iostream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <liboath/oath.h>
#include <cstdlib>
#include <ctime>

using json = nlohmann::json;

class AngelOneLogin {
private:
    std::string publicIP;
    std::string localIP;

public:
    AngelOneLogin(const std::string& pubIP, const std::string& locIP) : publicIP(pubIP), localIP(locIP) {}

    std::string sanitize(std::string str) {
        str.erase(str.find_last_not_of(" \n\r\t") + 1);
        str.erase(0, str.find_first_not_of(" \n\r\t"));
        return str;
    }

    struct Session {
        std::string jwtToken;
        std::string feedToken;
        bool success = false;
    };

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    Session login(const std::string& clientId, const std::string& password, const std::string& seed, const std::string& apiKey) {
        Session session;
        
        std::cout << "[INFO] Detected Public IP for Login: " << publicIP << std::endl;

        // 1. Correctly decode Base32 Seed
        char *key = nullptr;
        size_t keylen = 0;
        int decode_res = oath_base32_decode(seed.c_str(), seed.length(), &key, &keylen);
        
        if (decode_res != OATH_OK) {
            std::cerr << "[ERROR] Base32 decoding failed!" << std::endl;
            return session;
        }

        // 2. Generate 6-digit TOTP
        char totp_code[7];
        int totp_res = oath_totp_generate(key, keylen, std::time(nullptr), 30, 0, 6, totp_code);
        if (totp_res != OATH_OK) {
            std::cerr << "[ERROR] TOTP generation failed!" << std::endl;
            free(key);
            return session;
        }
        std::string totp = std::string(totp_code);
        
        // Free the memory allocated by oath_base32_decode
        free(key);
        
        std::cout << "[DEBUG] Generated TOTP: " << totp << std::endl;

        // 3. Perform REST API Login
        CURL* curl = curl_easy_init();
        if(curl) {
            std::string readBuffer;
            json payload;
            payload["clientcode"] = clientId;
            payload["password"] = password;
            payload["totp"] = totp;
            std::string data = payload.dump();

            struct curl_slist* headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, ("X-PrivateKey: " + sanitize(apiKey)).c_str());
            headers = curl_slist_append(headers, "X-UserType: USER");
            headers = curl_slist_append(headers, "X-SourceID: WEB");
            headers = curl_slist_append(headers, ("X-ClientLocalIP: " + localIP).c_str());
            headers = curl_slist_append(headers, ("X-ClientPublicIP: " + publicIP).c_str());
            headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
            headers = curl_slist_append(headers, "X-MACAddress: 02:00:00:00:00:00");
            headers = curl_slist_append(headers, "Accept: application/json");
            headers = curl_slist_append(headers, "Expect:");

            curl_easy_setopt(curl, CURLOPT_URL, "https://apiconnect.angelone.in/rest/auth/angelbroking/user/v1/loginByPassword");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

            if(curl_easy_perform(curl) == CURLE_OK) {
                std::cout << "[DEBUG] Server Response: " << readBuffer << std::endl;
                try {
                    auto response = json::parse(readBuffer);
                    if(response.contains("status") && response["status"].get<bool>()) {
                        session.jwtToken = response["data"]["jwtToken"];
                        session.feedToken = response["data"]["feedToken"];
                        session.success = true;
                    }
                } catch (...) {
                    std::cerr << "[ERROR] Failed to parse JSON response." << std::endl;
                }
            }
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }
        return session;
    }
};
#endif
