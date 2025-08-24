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
        std::string resp = "<html><head><title>AlcoControl</title></head><body>";
        resp += "<h1>AlcoControl Sensors</h1>";
        
        // Status grzałki
        resp += "<h2>Heater Status</h2>";
        resp += "<p><strong>Running:</strong> " + std::string(g_heater_manager->isRunning() ? "YES" : "NO") + "</p>";
        resp += "<p><strong>Current Temperature:</strong> " + std::to_string(g_main_temperature) + " &deg;C</p>";
        resp += "<p><strong>Target Temperature:</strong> " + std::to_string(PRODUCTION_TEMPERATURE_C) + " &deg;C</p>";
        resp += "<p><strong>Power Output:</strong> " + std::to_string(g_heater_manager->getCurrentPower()) + "%</p>";
        
        // Przyciski sterowania
        resp += "<div style='margin: 20px 0;'>";
        resp += "<button onclick=\"fetch('/api/heater/start', {method:'POST'})\">Start Heater</button> ";
        resp += "<button onclick=\"fetch('/api/heater/stop', {method:'POST'})\">Stop Heater</button>";
        resp += "</div>";
        
        // Sensory
        resp += "<h2>Sensors</h2><ul>";
        for (auto& s : *g_latest) {
            resp += "<li>" + s.id + ": " + std::to_string(s.value) + " &deg;C";
            if (s.id == g_main_sensor_id) {
                resp += " <strong>(Main)</strong>";
            }
            resp += "</li>";
        }
        resp += "</ul>";
        
        resp += "<script>setInterval(() => location.reload(), 5000);</script>"; // Auto-refresh co 5s
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