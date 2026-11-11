#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

extern "C" void app_main();
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "DS18B20Sensor.hpp"
#include "GpioOneWireTempBus.hpp"
#include "HttpServer.hpp"
#include "SensorManager.hpp"
#include "WifiManager.hpp"
#include "api.hpp"
#include "heater_manager.hpp"
#include "http_util.hpp"
#include "ota_manager.hpp"
#include "web_ui.hpp"

using namespace alco::web;

namespace {

// The heater must not switch itself on after a reset or a power cut while nobody is watching.
// Set to true only for a supervised bench setup.
constexpr bool kAutoStartHeater = false;
constexpr uint32_t kAutoStartDelayMs = 10000;

// Adapter: the HTTP routes only know the small IHeaterControl interface.
class HeaterControl : public IHeaterControl {
public:
    explicit HeaterControl(HeaterManager& h) : h_(h) {}
    void start() override { h_.start(); }
    void stop() override { h_.stop(); }
    bool running() const override { return h_.isRunning(); }
    float power() const override { return h_.getCurrentPower(); }
    float target() const override { return h_.getTargetTemperature(); }
    bool setTarget(float celsius) override { return h_.setTargetTemperature(celsius); }
    std::string faultText() const override { return h_.faultText(); }

private:
    HeaterManager& h_;
};

std::atomic<bool> g_wifiBusy{false};
std::atomic<bool> g_otaBusy{false};

struct WifiJob {
    std::string ssid;
    std::string password;
};

void wifiConnectTask(void* arg) {
    {
        std::unique_ptr<WifiJob> job(static_cast<WifiJob*>(arg));
        OTAManager::instance().connectToWiFi(job->ssid, job->password);   // blocks until it joined or failed
    }                                                    // the job is freed before the task deletes itself
    g_wifiBusy = false;
    vTaskDelete(nullptr);
}

void otaTask(void*) {
    OTAManager::instance().performUpdate();              // restarts the chip on success
    g_otaBusy = false;
    vTaskDelete(nullptr);
}

void registerPageRoutes(Router& router) {
    router.add(Method::Get, "/", [](const Request&) { return Response::html(dashboardPage()); });
    router.add(Method::Get, "/wifi", [](const Request&) { return Response::html(wifiPage()); });
    router.add(Method::Get, "/ota", [](const Request&) { return Response::html(otaPage()); });
}

void registerNetworkRoutes(Router& router, IHeaterControl& heater) {
    router.add(Method::Get, "/api/wifi/scan", [](const Request&) {
        const auto networks = OTAManager::instance().scanNetworks();
        std::string out = "{\"networks\":[";
        bool first = true;
        for (const auto& n : networks) {
            if (!first) out += ",";
            first = false;
            out += "{\"ssid\":\"" + jsonEscape(n.ssid) + "\",\"rssi\":" + std::to_string(n.rssi) +
                   ",\"secure\":" + (n.auth_mode != WIFI_AUTH_OPEN ? "true" : "false") +
                   ",\"connected\":" + (n.connected ? "true" : "false") + "}";
        }
        out += "]}";
        return Response::json(out);
    });

    // Joining a network blocks for seconds and can drop the access point the browser is using,
    // so it runs in its own task and the request is answered right away.
    router.add(Method::Post, "/api/wifi/connect", [](const Request& req) {
        const std::string* ssid = req.param("ssid");
        const std::string* pass = req.param("password");
        if (!ssid || ssid->empty() || ssid->size() > 32) return Response::error(400, "invalid ssid");
        if (pass && pass->size() > 63) return Response::error(400, "password too long");
        if (g_wifiBusy.exchange(true)) return Response::error(409, "a connection attempt is already running");

        auto* job = new WifiJob{*ssid, pass ? *pass : std::string()};
        if (xTaskCreate(wifiConnectTask, "wifi_connect", 6144, job, 4, nullptr) != pdPASS) {
            delete job;
            g_wifiBusy = false;
            return Response::error(500, "cannot start the connection task");
        }
        return Response::json(
            "{\"message\":\"Connecting. The device may drop this access point while it joins the network.\"}", 202);
    });

    router.add(Method::Get, "/api/ota/server", [](const Request&) {
        return Response::json("{\"server_url\":\"" + jsonEscape(OTAManager::instance().getOTAServer()) + "\"}");
    });

    router.add(Method::Post, "/api/ota/server", [](const Request& req) {
        const std::string* url = req.param("server_url");
        if (!url || !isValidHttpUrl(*url)) return Response::error(400, "invalid server URL (http:// or https://)");
        OTAManager::instance().setOTAServer(*url);
        return Response::json("{\"message\":\"Server URL saved\"}");
    });

    router.add(Method::Get, "/api/ota/check", [](const Request&) {
        auto& ota = OTAManager::instance();
        if (!ota.isConnectedToStation()) return Response::error(409, "not connected to a WiFi network");
        const OTAResult result = ota.checkForUpdate();
        const bool available = (result == OTAResult::SUCCESS);
        return Response::json(std::string("{\"available\":") + (available ? "true" : "false") +
                              ",\"message\":\"" + (available ? "Update available" : "No update or server unreachable") + "\"}");
    });

    router.add(Method::Post, "/api/ota/update", [&heater](const Request&) {
        auto& ota = OTAManager::instance();
        if (!ota.isConnectedToStation()) return Response::error(409, "not connected to a WiFi network");
        if (g_otaBusy.exchange(true)) return Response::error(409, "an update is already running");
        heater.stop();                                   // never flash firmware while the heater is on
        if (xTaskCreate(otaTask, "ota_update", 8192, nullptr, 5, nullptr) != pdPASS) {
            g_otaBusy = false;
            return Response::error(500, "cannot start the update task");
        }
        return Response::json("{\"success\":true,\"message\":\"Heater stopped, update started\"}");
    });

    router.add(Method::Get, "/api/ota/progress", [](const Request&) {
        auto& ota = OTAManager::instance();
        return Response::json("{\"progress\":" + std::to_string(ota.getUpdateProgress()) +
                              ",\"status\":\"" + jsonEscape(ota.getUpdateStatus()) + "\"}");
    });
}

} // namespace

extern "C" void app_main() {
    using alco::core::OneWireRom;

    // --- WiFi ---
    WifiManager::instance().init();

    // --- Heater ---
    static HeaterManager heater_manager;
    heater_manager.init();
    static HeaterControl heaterControl(heater_manager);
    static TelemetryStore telemetry;

    // --- DS18B20 bus ---
    auto bus = std::make_shared<alco::onewire::GpioOneWireTempBus>(
        GPIO_NUM_4, /*resolutionBits*/ 12, /*parasite*/ false, "1w-gpio4");
    std::vector<OneWireRom> found;
    auto st = bus->search(found);
    if (!st) printf("No DS18B20 found: %s\n", st.message.c_str());

    // --- Sensors ---
    static alco::core::SensorManager mgr("sensor_mgr", 1000);
    for (auto& dev : found) {
        mgr.add(std::make_unique<alco::ds18b20::DS18B20Sensor>(bus, dev, dev.rom));
    }
    mgr.subscribe([](const std::string& id, const alco::core::Reading& r) {
        // the first sensor that reports drives the heater
        if (telemetry.update(id, r.value, r.ts.ms)) heater_manager.updateTemperature(r.value);
        printf("[READ] %s = %.2f C @ %llu ms\n", id.c_str(), static_cast<double>(r.value),
               static_cast<unsigned long long>(r.ts.ms));
    });
    mgr.start();

    // --- HTTP: add all routes first, then start the server ---
    auto& http = HttpServer::instance();
    registerPageRoutes(http.router());
    registerHeaterRoutes(http.router(), heaterControl, telemetry);
    registerNetworkRoutes(http.router(), heaterControl);
    if (http.start() != ESP_OK) printf("HTTP server failed to start\n");

    if (kAutoStartHeater) {
        vTaskDelay(pdMS_TO_TICKS(kAutoStartDelayMs));
        heater_manager.start();
    }

    printf("AlcoControl started. AP: %s\n", WifiManager::instance().getApSsid().c_str());
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}
