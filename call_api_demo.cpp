#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <ctime>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string fetch_binance(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

int call_api_demo() {
    std::string symbol = "ETHUSDT";
    const std::string base_url = "https://fapi.binance.com/fapi/v1/continuousKlines?pair=" + symbol +
        "&contractType=PERPETUAL&interval=1m&limit=1500";
    std::string data = fetch_binance(url);
    if (data.empty()) return 1;
    json j;
    try {
        j = json::parse(data);
    }
    catch (...) {
        std::cerr << "JSON parse error" << std::endl;
        throw std::runtime_error("api JSON parse fatal error");
    }
    std::cout << data << std::endl;
    return 0;
}
