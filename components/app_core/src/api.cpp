#include "api.hpp"
#include "http_util.hpp"

namespace alco::web {

bool TelemetryStore::update(const std::string& id, float value, uint64_t tsMs) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (mainId_.empty()) mainId_ = id;
    bool found = false;
    for (auto& r : readings_) {
        if (r.id == id) {
            r.value = value;
            r.tsMs = tsMs;
            found = true;
            break;
        }
    }
    if (!found) readings_.push_back({id, value, tsMs});
    return id == mainId_;
}

std::vector<SensorReading> TelemetryStore::snapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return readings_;
}

bool TelemetryStore::mainReading(SensorReading& out) const {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& r : readings_) {
        if (r.id == mainId_) {
            out = r;
            return true;
        }
    }
    return false;
}

std::string sensorsJson(const std::vector<SensorReading>& readings) {
    std::string out = "{\"sensors\":[";
    bool first = true;
    for (const auto& r : readings) {
        if (!first) out += ",";
        first = false;
        out += "{\"id\":\"" + jsonEscape(r.id) + "\",\"value\":" + jsonNumber(r.value) +
               ",\"ts\":" + std::to_string(r.tsMs) + "}";
    }
    out += "]}";
    return out;
}

std::string heaterStatusJson(const IHeaterControl& heater, const TelemetryStore& telemetry) {
    SensorReading main{};
    const bool has = telemetry.mainReading(main);
    std::string out = "{\"running\":";
    out += heater.running() ? "true" : "false";
    out += ",\"temperature\":" + (has ? jsonNumber(main.value) : std::string("null"));
    out += ",\"target\":" + jsonNumber(heater.target());
    out += ",\"power\":" + jsonNumber(heater.power());
    out += ",\"fault\":\"" + jsonEscape(heater.faultText()) + "\"}";
    return out;
}

void registerHeaterRoutes(Router& router, IHeaterControl& heater, const TelemetryStore& telemetry) {
    router.add(Method::Get, "/api/sensors", [&telemetry](const Request&) {
        return Response::json(sensorsJson(telemetry.snapshot()));
    });

    router.add(Method::Get, "/api/heater/status", [&heater, &telemetry](const Request&) {
        return Response::json(heaterStatusJson(heater, telemetry));
    });

    router.add(Method::Post, "/api/heater/start", [&heater](const Request&) {
        heater.start();
        return Response::json(std::string("{\"running\":") + (heater.running() ? "true" : "false") + "}");
    });

    router.add(Method::Post, "/api/heater/stop", [&heater](const Request&) {
        heater.stop();
        return Response::json(std::string("{\"running\":") + (heater.running() ? "true" : "false") + "}");
    });

    router.add(Method::Post, "/api/heater/target", [&heater](const Request& req) {
        const std::string* v = req.param("value");
        float t = 0.0f;
        if (!v || !parseFloat(*v, t)) return Response::error(400, "value must be a number");
        if (!heater.setTarget(t)) return Response::error(400, "target out of range");
        return Response::json("{\"target\":" + jsonNumber(heater.target()) + "}");
    });
}

} // namespace alco::web
