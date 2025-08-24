#include "DS18B20Sensor.hpp"

using namespace alco::core;
using namespace alco::ds18b20;

DS18B20Sensor::DS18B20Sensor(std::shared_ptr<IOneWireTempBus> bus,
                             OneWireRom rom,
                             std::string alias)
: bus_(std::move(bus)), rom_(std::move(rom)), alias_(std::move(alias)) {}

std::string DS18B20Sensor::id() const {
    return alias_.empty() ? rom_.rom : alias_;
}

Status DS18B20Sensor::init() {
    if (!bus_) return {StatusCode::InvalidState, "null bus"};
    return bus_->init();
}

Status DS18B20Sensor::read(Reading& out) {
    float t{};
    auto st = readCelsius(t);
    if (!st) return st;
    out.value = t;
    out.ts.ms = nowMs();
    return Status::ok();
}

Status DS18B20Sensor::readCelsius(float& out) {
    if (!bus_) return {StatusCode::InvalidState, "null bus"};

    const uint64_t now = nowMs();
    if (cacheTtlMs_ > 0 && hasCache_ && (now - lastTsMs_ <= cacheTtlMs_)) {
        out = lastC_;
        return Status::ok();
    }

    // Zrób konwersję + odczyt
    auto st = bus_->startConversion(rom_);
    if (!st) return st;

    float c{};
    st = bus_->readTemperature(rom_, c);
    if (!st) return st;

    // kalibracja (offset w °C)
    c += offsetC_;

    // zaktualizuj cache
    lastC_ = c;
    lastTsMs_ = nowMs();
    hasCache_ = true;

    out = c;
    return Status::ok();
}
