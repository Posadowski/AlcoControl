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

extern "C" void app_main() {
    using alco::core::OneWireRom;

    // --- WiFi ---
    WifiManager::instance().init();

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

    struct SensorCache {
        std::string id;
        float value;
        uint64_t ts;
    };
    std::vector<SensorCache> latest;

    mgr.subscribe([&latest](const std::string& id, const alco::core::Reading& r){
        // Zapisz ostatni odczyt w bufory
        bool found = false;
        for (auto& s : latest) {
            if (s.id == id) {
                s.value = r.value;
                s.ts = r.ts.ms;
                found = true;
                break;
            }
        }
        if (!found) latest.push_back({id, r.value, r.ts.ms});

        // Dla debugu możesz też printować jak wcześniej
        printf("[READ] %s = %.2f °C @ %llu ms\n", id.c_str(), r.value, r.ts.ms);
    });
    mgr.start();

        // --- HTTP Server ---
    auto& http = HttpServer::instance();
    http.start();

    // Root endpoint (HTML)
    http.registerEndpoint("/", HTTP_GET, [&latest]() {
        std::string resp = "<html><head><title>AlcoControl</title></head><body>";
        resp += "<h1>AlcoControl Sensors</h1><ul>";
        for (auto& s : latest) {
            resp += "<li>" + s.id + ": " + std::to_string(s.value) + " &deg;C</li>";
        }
        resp += "</ul></body></html>";
        return resp;
    });

    // API JSON
    http.registerEndpoint("/api/sensors", HTTP_GET, [&latest]() {
        std::string resp = "{ \"sensors\": [";
        bool first = true;
        for (auto& s : latest) {
            if (!first) resp += ",";
            resp += "{ \"id\": \"" + s.id + "\", \"value\": " + std::to_string(s.value) + " }";
            first = false;
        }
        resp += "]}";
        return resp;
    });

    while (true) vTaskDelay(pdMS_TO_TICKS(10000));
}