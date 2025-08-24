#pragma once
#include <string>
#include <vector>
#include <functional>
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_app_format.h"

struct WiFiNetwork {
    std::string ssid;
    int rssi;
    wifi_auth_mode_t auth_mode;
    bool connected;
};

enum class OTAResult {
    SUCCESS,
    DOWNLOAD_FAILED,
    PARTITION_ERROR,
    VALIDATION_FAILED,
    NETWORK_ERROR
};

class OTAManager {
public:
    static OTAManager& instance();
    
    // WiFi scanning
    std::vector<WiFiNetwork> scanNetworks();
    bool connectToWiFi(const std::string& ssid, const std::string& password);
    bool isConnectedToStation();
    
    // OTA update
    void setOTAServer(const std::string& server_url);
    std::string getOTAServer();
    OTAResult checkForUpdate();
    OTAResult performUpdate();
    void setProgressCallback(std::function<void(int)> callback);
    
    // Config persistence
    bool saveWiFiConfig(const std::string& ssid, const std::string& password);
    bool loadWiFiConfig(std::string& ssid, std::string& password);
    bool saveOTAConfig(const std::string& server_url);
    bool loadOTAConfig(std::string& server_url);
    
    // Status
    std::string getUpdateStatus();
    int getUpdateProgress();
    
private:
    OTAManager() = default;
    
    std::string ota_server_url;
    std::string update_status;
    int update_progress;
    std::function<void(int)> progress_callback;
    
    static esp_err_t http_event_handler(esp_http_client_event_t *evt);
};