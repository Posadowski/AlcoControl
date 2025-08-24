#include "ota_manager.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <cstring>

static const char* TAG = "OTA_MGR";

// NVS keys
#define NVS_NAMESPACE "ota_config"
#define NVS_WIFI_SSID "wifi_ssid"
#define NVS_WIFI_PASS "wifi_pass"
#define NVS_OTA_SERVER "ota_server"

// WiFi events
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static const int MAXIMUM_RETRY = 5;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

OTAManager& OTAManager::instance() {
    static OTAManager instance;
    return instance;
}

std::vector<WiFiNetwork> OTAManager::scanNetworks() {
    std::vector<WiFiNetwork> networks;
    
    // Configure WiFi for scanning
    wifi_scan_config_t scan_config = {
        .ssid = nullptr,
        .bssid = nullptr,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {.active = {.min = 100, .max = 300}}
    };
    
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    
    uint16_t number = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&number));
    
    if (number > 0) {
        wifi_ap_record_t* ap_info = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * number);
        if (ap_info) {
            ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
            
            for (int i = 0; i < number; i++) {
                WiFiNetwork network;
                network.ssid = std::string((char*)ap_info[i].ssid);
                network.rssi = ap_info[i].rssi;
                network.auth_mode = ap_info[i].authmode;
                network.connected = false;
                
                // Check if this is the currently connected network
                wifi_ap_record_t current_ap;
                if (esp_wifi_sta_get_ap_info(&current_ap) == ESP_OK) {
                    if (strcmp((char*)current_ap.ssid, (char*)ap_info[i].ssid) == 0) {
                        network.connected = true;
                    }
                }
                
                networks.push_back(network);
            }
            free(ap_info);
        }
    }
    
    return networks;
}

bool OTAManager::connectToWiFi(const std::string& ssid, const std::string& password) {
    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
    }
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        nullptr,
                                                        nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        nullptr,
                                                        nullptr));
    
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password.c_str(), sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "wifi_init_sta finished.");
    
    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by wifi_event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                          pdFALSE,
                                          pdFALSE,
                                          portMAX_DELAY);
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", ssid.c_str());
        saveWiFiConfig(ssid, password);
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", ssid.c_str());
    }
    
    return false;
}

bool OTAManager::isConnectedToStation() {
    wifi_ap_record_t ap_info;
    return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
}

void OTAManager::setOTAServer(const std::string& server_url) {
    ota_server_url = server_url;
    saveOTAConfig(server_url);
}

std::string OTAManager::getOTAServer() {
    if (ota_server_url.empty()) {
        loadOTAConfig(ota_server_url);
    }
    return ota_server_url;
}

OTAResult OTAManager::checkForUpdate() {
    if (ota_server_url.empty()) {
        ESP_LOGE(TAG, "OTA server URL not set");
        return OTAResult::NETWORK_ERROR;
    }
    
    // Simple check - w rzeczywistości możesz sprawdzić wersję na serwerze
    std::string check_url = ota_server_url + "/version.txt";
    
    esp_http_client_config_t config = {};
    config.url = check_url.c_str();
    config.timeout_ms = 5000;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            ESP_LOGI(TAG, "Update available");
            esp_http_client_cleanup(client);
            return OTAResult::SUCCESS;
        }
    }
    
    esp_http_client_cleanup(client);
    ESP_LOGE(TAG, "Failed to check for updates");
    return OTAResult::NETWORK_ERROR;
}

esp_err_t OTAManager::http_event_handler(esp_http_client_event_t *evt) {
    OTAManager* manager = static_cast<OTAManager*>(evt->user_data);
    
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // Update progress based on content length
            if (esp_http_client_get_content_length(evt->client) > 0) {
                int progress = (evt->data_len * 100) / esp_http_client_get_content_length(evt->client);
                manager->update_progress = progress;
                if (manager->progress_callback) {
                    manager->progress_callback(progress);
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

OTAResult OTAManager::performUpdate() {
    if (ota_server_url.empty()) {
        ESP_LOGE(TAG, "OTA server URL not set");
        return OTAResult::NETWORK_ERROR;
    }
    
    update_status = "Starting update...";
    update_progress = 0;
    
    std::string firmware_url = ota_server_url + "/firmware.bin";
    
    esp_http_client_config_t config = {};
    config.url = firmware_url.c_str();
    config.timeout_ms = 30000;
    config.event_handler = http_event_handler;
    config.user_data = this;
    
    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &config;
    
    ESP_LOGI(TAG, "Starting OTA update from: %s", firmware_url.c_str());
    update_status = "Downloading firmware...";
    
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        update_status = "Update completed successfully";
        update_progress = 100;
        ESP_LOGI(TAG, "OTA update successful, restarting...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
        return OTAResult::SUCCESS;
    } else {
        update_status = "Update failed: " + std::string(esp_err_to_name(ret));
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(ret));
        return OTAResult::DOWNLOAD_FAILED;
    }
}

void OTAManager::setProgressCallback(std::function<void(int)> callback) {
    progress_callback = callback;
}

std::string OTAManager::getUpdateStatus() {
    return update_status;
}

int OTAManager::getUpdateProgress() {
    return update_progress;
}

bool OTAManager::saveWiFiConfig(const std::string& ssid, const std::string& password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return false;
    
    err = nvs_set_str(nvs_handle, NVS_WIFI_SSID, ssid.c_str());
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, NVS_WIFI_PASS, password.c_str());
    }
    
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    return (err == ESP_OK);
}

bool OTAManager::loadWiFiConfig(std::string& ssid, std::string& password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return false;
    
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, NVS_WIFI_SSID, nullptr, &required_size);
    if (err == ESP_OK && required_size > 0) {
        char* ssid_buf = (char*)malloc(required_size);
        if (ssid_buf) {
            err = nvs_get_str(nvs_handle, NVS_WIFI_SSID, ssid_buf, &required_size);
            if (err == ESP_OK) {
                ssid = std::string(ssid_buf);
            }
            free(ssid_buf);
        }
    }
    
    required_size = 0;
    err = nvs_get_str(nvs_handle, NVS_WIFI_PASS, nullptr, &required_size);
    if (err == ESP_OK && required_size > 0) {
        char* pass_buf = (char*)malloc(required_size);
        if (pass_buf) {
            err = nvs_get_str(nvs_handle, NVS_WIFI_PASS, pass_buf, &required_size);
            if (err == ESP_OK) {
                password = std::string(pass_buf);
            }
            free(pass_buf);
        }
    }
    
    nvs_close(nvs_handle);
    return !ssid.empty();
}

bool OTAManager::saveOTAConfig(const std::string& server_url) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return false;
    
    err = nvs_set_str(nvs_handle, NVS_OTA_SERVER, server_url.c_str());
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    return (err == ESP_OK);
}

bool OTAManager::loadOTAConfig(std::string& server_url) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return false;
    
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, NVS_OTA_SERVER, nullptr, &required_size);
    if (err == ESP_OK && required_size > 0) {
        char* url_buf = (char*)malloc(required_size);
        if (url_buf) {
            err = nvs_get_str(nvs_handle, NVS_OTA_SERVER, url_buf, &required_size);
            if (err == ESP_OK) {
                server_url = std::string(url_buf);
            }
            free(url_buf);
        }
    }
    
    nvs_close(nvs_handle);
    return !server_url.empty();
}