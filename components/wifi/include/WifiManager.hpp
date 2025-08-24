#pragma once
#include "esp_wifi.h"
#include <string>

class WifiManager {
public:
    static WifiManager& instance();

    void init(const std::string& sta_ssid = "", const std::string& sta_pass = "");
    std::string getApSsid() const;

private:
    WifiManager() = default;
    std::string ap_ssid_;
};
