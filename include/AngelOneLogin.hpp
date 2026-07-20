#ifndef ANGEL_ONE_LOGIN_HPP
#define ANGEL_ONE_LOGIN_HPP

#include <iostream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <cmath>
#include <cctype>

using json = nlohmann::json;

class AngelOneLogin {
private:
    std::string publicIP;
    std::string localIP;

    // Internal Base32 decoder to remove liboath dependency
    std::vector<uint8_t> base32_decode(const std::string& input) {
        static const std::string base32_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        std::vector<uint8_t> out;
        uint32_t buffer = 0;
        int bitsLeft = 0;
        for (char c : input) {
            if (std::isspace(static_cast<unsigned char>(c))) continue;
            auto val = base32_chars.find(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            if (val == std::string::npos) continue;
            buffer = (buffer << 5) | (val & 0x1F);
            bitsLeft += 5;
            if (bitsLeft >= 8) {
                out.push_back(static_cast<uint8_t>((buffer >> (bitsLeft - 8)) & 0xFF));
                bitsLeft -= 8;
            }
        }
        return out;
    }

    // Internal TOTP generator using OpenSSL (already required for WebSockets)
    std::string generateTOTP(const std::string& seed) {
        auto key = base32_decode(seed);
        uint64_t timer = std::time(nullptr) / 30;
        uint8_t data[8];
        for (int i = 7; i >= 0; i--) {
            data[i] = timer & 0xFF;
            timer >>= 8;
        }

        unsigned int len = 20;
        unsigned char hash[20];
        HMAC(EVP_sha1(), key.data(), key.size(), data, 8, hash, &len);

        int offset = hash[19] & 0x0F;
        uint32_t truncatedHash = (hash[offset] & 0x7F) << 24 | (hash[offset + 1] & 0xFF) << 16 | (hash[offset + 2] & 0xFF) << 8 | (hash[offset + 3] & 0xFF);
        
        uint32_t pin = truncatedHash % 1000000;
        std::string res = std::to_string(pin);
        while (res.length() < 6) res = "0" + res;
        return res;
    }

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

        // Generate 6-digit TOTP internally using OpenSSL
        std::string totp = generateTOTP(seed);
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
                    bool isOk = (response.contains("status") && response["status"].is_boolean() && response["status"].get<bool>()) ||
                               (response.contains("success") && response["success"].is_boolean() && response["success"].get<bool>());
                    
                    if(isOk) {
                        session.jwtToken = response["data"]["jwtToken"];
                        session.feedToken = response["data"]["feedToken"];
                        session.success = true;
                    } else {
                        std::string msg = response.contains("message") ? response["message"].get<std::string>() : "Unknown Error";
                        std::cerr << "[LOGIN] API Error: " << msg << std::endl;
                    }
                } catch (const json::parse_error& e) {
                    std::cerr << "[ERROR] JSON Parse Error: " << e.what() << std::endl;
                    std::cerr << "[ERROR] Raw Body: " << readBuffer << std::endl;
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
