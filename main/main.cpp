#include <memory>
#include <vector>
#include <cstdio>

extern "C" {
void app_main();
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include "SensorManager.hpp"
#include "GpioOneWireTempBus.hpp"
#include "DS18B20Sensor.hpp"
#include "WifiManager.hpp"
#include "HttpServer.hpp"
#include "heater_manager.hpp" 
#include "ota_manager.hpp"

// Globalne zmienne (poza app_main aby uniknąć capture warnings)
static HeaterManager* g_heater_manager = nullptr;
static float g_main_temperature = 25.0f;
static std::string g_main_sensor_id;
static std::vector<struct SensorCache>* g_latest = nullptr;

struct SensorCache {
    std::string id;
    float value;
    uint64_t ts;
};


extern "C" void app_main() {
    using alco::core::OneWireRom;

    // --- WiFi ---
    WifiManager::instance().init();

    // --- Heater Manager ---
    static HeaterManager heater_manager;
    g_heater_manager = &heater_manager;
    heater_manager.init();

    auto bus = std::make_shared<alco::onewire::GpioOneWireTempBus>(
        GPIO_NUM_4, /*resolutionBits*/ 12, /*parasite*/ false, "1w-gpio4"
    );

    // Wyszukaj sondy
    std::vector<OneWireRom> found;
    auto st = bus->search(found);
    if (!st) {
        printf("No DS18B20 found: %s\n", st.message.c_str());
        // Możesz mimo to kontynuować z ręcznie wpisanym ROMem
    }

    alco::core::SensorManager mgr("sensor_mgr", /*periodMs*/ 1000);

    // Dodaj wszystkie znalezione
    for (auto& dev : found) {
        auto s = std::make_unique<alco::ds18b20::DS18B20Sensor>(bus, dev);
        s->setOffset(0.35f);       // przykład kalibracji: +0.35°C
        s->setCacheTtlMs(750);     // cache dopasowany do 12-bit (750ms)
        mgr.add(std::move(s));
    }


    std::vector<SensorCache> latest;
    g_latest = &latest;


    mgr.subscribe([](const std::string& id, const alco::core::Reading& r){
        // Zapisz ostatni odczyt w bufory
        bool found = false;
        for (auto& s : *g_latest) {
            if (s.id == id) {
                s.value = r.value;
                s.ts = r.ts.ms;
                found = true;
                break;
            }
        }
        if (!found) g_latest->push_back({id, r.value, r.ts.ms});

        // Aktualizuj temperaturę w heater manager (pierwszy sensor jako główny)
        if (g_main_sensor_id.empty() || g_main_sensor_id == id) {
            g_main_sensor_id = id;
            g_main_temperature = r.value;
            g_heater_manager->updateTemperature(r.value);
        }

        // Dla debugu możesz też printować jak wcześniej
        printf("[READ] %s = %.2f °C @ %llu ms\n", id.c_str(), r.value, r.ts.ms);
    });
    mgr.start();

        // --- HTTP Server ---
    auto& http = HttpServer::instance();
    http.start();

    // Root endpoint (HTML) - rozszerzony o status grzałki
    http.registerEndpoint("/", HTTP_GET, []() {
        std::string resp = "<html><head><title>AlcoControl</title>";
        resp += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        resp += "<style>body{font-family:Arial;margin:20px} .btn{padding:10px;margin:5px;cursor:pointer;background:#007cba;color:white;border:none;border-radius:4px} .section{margin:20px 0;padding:15px;border:1px solid #ddd;border-radius:5px}</style>";
        resp += "</head><body>";
        resp += "<h1>AlcoControl Dashboard</h1>";
        
        // Navigation
        resp += "<div style='margin-bottom:20px'>";
        resp += "<button class='btn' onclick='location.href=\"/\"'>Dashboard</button> ";
        resp += "<button class='btn' onclick='location.href=\"/wifi\"'>WiFi Config</button> ";
        resp += "<button class='btn' onclick='location.href=\"/ota\"'>OTA Update</button>";
        resp += "</div>";
        
        // Status grzałki
        resp += "<div class='section'><h2>Heater Status</h2>";
        resp += "<p><strong>Running:</strong> " + std::string(g_heater_manager->isRunning() ? "YES" : "NO") + "</p>";
        resp += "<p><strong>Current Temperature:</strong> " + std::to_string(g_main_temperature) + " &deg;C</p>";
        resp += "<p><strong>Target Temperature:</strong> " + std::to_string(PRODUCTION_TEMPERATURE_C) + " &deg;C</p>";
        resp += "<p><strong>Power Output:</strong> " + std::to_string(g_heater_manager->getCurrentPower()) + "%</p>";
        
        // Przyciski sterowania
        resp += "<div style='margin: 15px 0;'>";
        resp += "<button class='btn' onclick=\"fetch('/api/heater/start', {method:'POST'})\">Start Heater</button> ";
        resp += "<button class='btn' onclick=\"fetch('/api/heater/stop', {method:'POST'})\" style='background:#dc3545'>Stop Heater</button>";
        resp += "</div></div>";
        
        // Sensory
        resp += "<div class='section'><h2>Sensors</h2><ul>";
        for (auto& s : *g_latest) {
            resp += "<li>" + s.id + ": " + std::to_string(s.value) + " &deg;C";
            if (s.id == g_main_sensor_id) {
                resp += " <strong>(Main)</strong>";
            }
            resp += "</li>";
        }
        resp += "</ul></div>";
        
        resp += "<script>setInterval(() => location.reload(), 10000);</script>"; // Auto-refresh co 10s
        resp += "</body></html>";
        return resp;
    });

    // API JSON - rozszerzony o status grzałki
    http.registerEndpoint("/api/sensors", HTTP_GET, []() {
        std::string resp = "{ \"sensors\": [";
        bool first = true;
        for (auto& s : *g_latest) {
            if (!first) resp += ",";
            resp += "{ \"id\": \"" + s.id + "\", \"value\": " + std::to_string(s.value) + " }";
            first = false;
        }
        resp += "]}";
        return resp;
    });

    // API - Status grzałki
    http.registerEndpoint("/api/heater/status", HTTP_GET, []() {
        std::string resp = "{ ";
        resp += "\"running\": " + std::string(g_heater_manager->isRunning() ? "true" : "false") + ", ";
        resp += "\"temperature\": " + std::to_string(g_main_temperature) + ", ";
        resp += "\"target\": " + std::to_string(PRODUCTION_TEMPERATURE_C) + ", ";
        resp += "\"power\": " + std::to_string(g_heater_manager->getCurrentPower());
        resp += " }";
        return resp;
    });

    // API - Start grzałki
    http.registerEndpoint("/api/heater/start", HTTP_POST, []() {
        g_heater_manager->start();
        return std::string("{ \"status\": \"started\" }");
    });

    // API - Stop grzałki  
    http.registerEndpoint("/api/heater/stop", HTTP_POST, []() {
        g_heater_manager->stop();
        return std::string("{ \"status\": \"stopped\" }");
    });

    // === WiFi Configuration Pages ===
    http.registerEndpoint("/wifi", HTTP_GET, []() {
        auto& ota = OTAManager::instance();
        auto networks = ota.scanNetworks();
        
        std::string resp = "<html><head><title>AlcoControl - WiFi Config</title>";
        resp += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        resp += "<style>body{font-family:Arial;margin:20px} .btn{padding:10px;margin:5px;cursor:pointer;background:#007cba;color:white;border:none;border-radius:4px} .network{margin:10px 0;padding:10px;border:1px solid #ddd;border-radius:4px} .connected{background:#d4edda} input{padding:8px;margin:5px;border:1px solid #ccc;border-radius:4px;width:90%}</style>";
        resp += "</head><body>";
        resp += "<h1>WiFi Configuration</h1>";
        
        // Navigation
        resp += "<div style='margin-bottom:20px'>";
        resp += "<button class='btn' onclick='location.href=\"/\"'>Dashboard</button> ";
        resp += "<button class='btn' onclick='location.href=\"/wifi\"' style='background:#28a745'>WiFi Config</button> ";
        resp += "<button class='btn' onclick='location.href=\"/ota\"'>OTA Update</button>";
        resp += "</div>";
        
        // Current connection status
        if (ota.isConnectedToStation()) {
            resp += "<div class='network connected'><strong>✓ Connected to WiFi</strong></div>";
        } else {
            resp += "<div class='network' style='background:#f8d7da'><strong>⚠ Not connected to WiFi</strong></div>";
        }
        
        resp += "<h3>Available Networks:</h3>";
        for (auto& network : networks) {
            resp += "<div class='network" + std::string(network.connected ? " connected" : "") + "'>";
            resp += "<strong>" + network.ssid + "</strong> (RSSI: " + std::to_string(network.rssi) + " dBm)";
            if (network.auth_mode != WIFI_AUTH_OPEN) resp += " 🔒";
            if (network.connected) {
                resp += " <span style='color:green'>✓ Connected</span>";
            } else {
                resp += "<br><form style='margin-top:10px' action='/api/wifi/connect' method='post'>";
                resp += "<input type='hidden' name='ssid' value='" + network.ssid + "'>";
                if (network.auth_mode != WIFI_AUTH_OPEN) {
                    resp += "<input type='password' name='password' placeholder='Password' required>";
                }
                resp += "<button type='submit' class='btn'>Connect</button></form>";
            }
            resp += "</div>";
        }
        
        resp += "<button class='btn' onclick='location.reload()' style='margin-top:20px'>Refresh Networks</button>";
        resp += "</body></html>";
        return resp;
    });

    // === OTA Update Page ===
    http.registerEndpoint("/ota", HTTP_GET, []() {
        auto& ota = OTAManager::instance();
        
        std::string resp = "<html><head><title>AlcoControl - OTA Update</title>";
        resp += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        resp += "<style>body{font-family:Arial;margin:20px} .btn{padding:10px;margin:5px;cursor:pointer;background:#007cba;color:white;border:none;border-radius:4px} .section{margin:20px 0;padding:15px;border:1px solid #ddd;border-radius:5px} input{padding:8px;margin:5px;border:1px solid #ccc;border-radius:4px;width:90%} .progress{width:100%;height:20px;background:#f0f0f0;border-radius:10px;overflow:hidden} .progress-bar{height:100%;background:#007cba;transition:width 0.3s}</style>";
        resp += "</head><body>";
        resp += "<h1>OTA Update</h1>";
        
        // Navigation
        resp += "<div style='margin-bottom:20px'>";
        resp += "<button class='btn' onclick='location.href=\"/\"'>Dashboard</button> ";
        resp += "<button class='btn' onclick='location.href=\"/wifi\"'>WiFi Config</button> ";
        resp += "<button class='btn' onclick='location.href=\"/ota\"' style='background:#28a745'>OTA Update</button>";
        resp += "</div>";
        
        // Connection status
        if (!ota.isConnectedToStation()) {
            resp += "<div class='section' style='background:#f8d7da'>";
            resp += "<h3>⚠ WiFi Required</h3>";
            resp += "<p>OTA updates require an internet connection. Please configure WiFi first.</p>";
            resp += "<button class='btn' onclick='location.href=\"/wifi\"'>Configure WiFi</button>";
            resp += "</div>";
        } else {
            // Server configuration
            resp += "<div class='section'>";
            resp += "<h3>Update Server Configuration</h3>";
            resp += "<form action='/api/ota/server' method='post'>";
            resp += "<label>Server URL:</label><br>";
            resp += "<input type='url' name='server_url' value='" + ota.getOTAServer() + "' placeholder='https://example.com/firmware' required><br>";
            resp += "<small>Enter the URL where your firmware.bin file is hosted</small><br>";
            resp += "<button type='submit' class='btn' style='margin-top:10px'>Save Server</button>";
            resp += "</form></div>";
            
            // Update controls
            resp += "<div class='section'>";
            resp += "<h3>Firmware Update</h3>";
            resp += "<p><strong>Status:</strong> <span id='status'>" + ota.getUpdateStatus() + "</span></p>";
            
            // Progress bar
            resp += "<div class='progress' style='margin:10px 0'>";
            resp += "<div class='progress-bar' id='progressBar' style='width:" + std::to_string(ota.getUpdateProgress()) + "%'></div>";
            resp += "</div>";
            resp += "<p><span id='progress'>" + std::to_string(ota.getUpdateProgress()) + "</span>%</p>";
            
            resp += "<button class='btn' onclick='checkUpdate()'>Check for Update</button> ";
            resp += "<button class='btn' onclick='startUpdate()' style='background:#ffc107;color:#000' id='updateBtn'>Start Update</button>";
            resp += "</div>";
        }
        
        resp += R"(
        <script>
        function checkUpdate() {
            document.getElementById('status').innerText = 'Checking for updates...';
            fetch('/api/ota/check')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('status').innerText = data.message;
                    document.getElementById('updateBtn').disabled = !data.available;
                });
        }
        
        function startUpdate() {
            if(confirm('This will update the firmware and restart the device. Continue?')) {
                document.getElementById('status').innerText = 'Starting update...';
                document.getElementById('updateBtn').disabled = true;
                
                fetch('/api/ota/update', {method: 'POST'})
                    .then(response => response.json())
                    .then(data => {
                        document.getElementById('status').innerText = data.message;
                        if(data.success) {
                            // Poll for progress updates
                            pollProgress();
                        }
                    });
            }
        }
        
        function pollProgress() {
            const interval = setInterval(() => {
                fetch('/api/ota/progress')
                    .then(response => response.json())
                    .then(data => {
                        document.getElementById('progress').innerText = data.progress;
                        document.getElementById('progressBar').style.width = data.progress + '%';
                        document.getElementById('status').innerText = data.status;
                        
                        if(data.progress >= 100 || data.status.includes('failed')) {
                            clearInterval(interval);
                        }
                    });
            }, 1000);
        }
        </script>
        )";
        
        resp += "</body></html>";
        return resp;
    });

    printf("AlcoControl started. Temperature target: %.1f°C\n", PRODUCTION_TEMPERATURE_C);
    
    // === API Endpoints for WiFi and OTA ===
    
    // WiFi Connect API - Note: Your HttpServer needs to support POST with form data
    // For now, we'll use simple GET endpoints with parameters
    http.registerEndpoint("/api/wifi/scan", HTTP_GET, []() {
        auto& ota = OTAManager::instance();
        auto networks = ota.scanNetworks();
        
        std::string resp = "{ \"networks\": [";
        bool first = true;
        for (auto& network : networks) {
            if (!first) resp += ",";
            resp += "{ \"ssid\": \"" + network.ssid + "\", ";
            resp += "\"rssi\": " + std::to_string(network.rssi) + ", ";
            resp += "\"auth\": " + std::to_string(network.auth_mode) + ", ";
            resp += "\"connected\": " + std::string(network.connected ? "true" : "false") + " }";
            first = false;
        }
        resp += "]}";
        return resp;
    });
    
    // OTA Server Config
    http.registerEndpoint("/api/ota/server/set", HTTP_GET, []() {
        // URL parameter extraction would be needed here
        // For simplicity, using a fixed example
        
        //auto& ota = OTAManager::instance();

        // This would need proper parameter parsing
        // ota.setOTAServer(server_url_from_params);
        return std::string("{ \"status\": \"Server URL saved\" }");
    });
    
    // Check for updates
    http.registerEndpoint("/api/ota/check", HTTP_GET, []() {
        auto& ota = OTAManager::instance();
        OTAResult result = ota.checkForUpdate();
        
        std::string resp = "{ ";
        resp += "\"available\": " + std::string(result == OTAResult::SUCCESS ? "true" : "false") + ", ";
        resp += "\"message\": \"";
        
        switch(result) {
            case OTAResult::SUCCESS:
                resp += "Update available";
                break;
            case OTAResult::NETWORK_ERROR:
                resp += "Network error - check server URL and WiFi";
                break;
            default:
                resp += "No update available";
                break;
        }
        resp += "\" }";
        return resp;
    });
    
    // Start OTA update
    http.registerEndpoint("/api/ota/update", HTTP_POST, []() {
        auto& ota = OTAManager::instance();
        
        // Start update in background task
        xTaskCreate([](void* param) {
            auto* ota_mgr = static_cast<OTAManager*>(param);
            ota_mgr->performUpdate();
            vTaskDelete(nullptr);
        }, "ota_update", 8192, &ota, 5, nullptr);
        
        return std::string("{ \"success\": true, \"message\": \"Update started\" }");
    });
    
    // Get update progress
    http.registerEndpoint("/api/ota/progress", HTTP_GET, []() {
        auto& ota = OTAManager::instance();
        std::string resp = "{ ";
        resp += "\"progress\": " + std::to_string(ota.getUpdateProgress()) + ", ";
        resp += "\"status\": \"" + ota.getUpdateStatus() + "\" }";
        return resp;
    });

    printf("AlcoControl started. Temperature target: %.1f°C\n", PRODUCTION_TEMPERATURE_C);
    
    // Automatyczne uruchomienie grzałki po 10 sekundach (daj czas na ustabilizowanie się odczytów)
    xTaskCreate([](void* param) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 sekund
        HeaterManager* heater = static_cast<HeaterManager*>(param);
        printf("Auto-starting heater...\n");
        heater->start();
        vTaskDelete(nullptr); // Ważne: usunięcie taska po zakończeniu
    }, "auto_start", 2048, &heater_manager, 2, nullptr);



    while (true) vTaskDelay(pdMS_TO_TICKS(10000));
}