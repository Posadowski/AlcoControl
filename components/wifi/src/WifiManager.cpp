#include "WifiManager.hpp"

#include <string.h>
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char* TAG = "WifiManager";

WifiManager& WifiManager::instance() {
    static WifiManager inst;
    return inst;
}

void WifiManager::init(const std::string& sta_ssid, const std::string& sta_pass) {
    // Init NVS
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Generate AP SSID with MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "AlcoControlAP-%02X%02X%02X", mac[3], mac[4], mac[5]);
    ap_ssid_ = ssid;

    wifi_config_t ap_config = {};
    strcpy((char*)ap_config.ap.ssid, ap_ssid_.c_str());
    ap_config.ap.ssid_len = ap_ssid_.length();
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    wifi_config_t sta_config = {};
    if (!sta_ssid.empty()) {
        strcpy((char*)sta_config.sta.ssid, sta_ssid.c_str());
        strcpy((char*)sta_config.sta.password, sta_pass.c_str());
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    if (!sta_ssid.empty()) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started with SSID: %s", ap_ssid_.c_str());
}

std::string WifiManager::getApSsid() const {
    return ap_ssid_;
}
