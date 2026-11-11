#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include "router.hpp"

namespace alco::web {

struct SensorReading {
    std::string id;
    float value;
    uint64_t tsMs;
};

// Latest reading per sensor. Written by the sensor task, read by the HTTP task.
// The first sensor that ever reports becomes the "main" one that drives the heater.
class TelemetryStore {
public:
    // Returns true if this reading comes from the main sensor.
    bool update(const std::string& id, float value, uint64_t tsMs);
    std::vector<SensorReading> snapshot() const;
    bool mainReading(SensorReading& out) const;

private:
    mutable std::mutex mtx_;
    std::vector<SensorReading> readings_;
    std::string mainId_;
};

// What the HTTP routes need from the heater. HeaterManager gets a thin adapter,
// tests use a fake, so the routes can be tested without hardware.
class IHeaterControl {
public:
    virtual ~IHeaterControl() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
    virtual float power() const = 0;
    virtual float target() const = 0;
    virtual bool setTarget(float celsius) = 0;       // false if rejected
    virtual std::string faultText() const = 0;
};

std::string sensorsJson(const std::vector<SensorReading>& readings);
std::string heaterStatusJson(const IHeaterControl& heater, const TelemetryStore& telemetry);

// GET /api/sensors, GET /api/heater/status, POST /api/heater/{start,stop,target}
// The references must outlive the router.
void registerHeaterRoutes(Router& router, IHeaterControl& heater, const TelemetryStore& telemetry);

} // namespace alco::web
