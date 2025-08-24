#pragma once
#include <string>
#include <memory>
#include <cstdint>
#include "ITemperatureSensor.hpp"
#include "IOneWireTempBus.hpp"

extern "C" {
#include "esp_timer.h"
}

namespace alco::ds18b20 {

class DS18B20Sensor : public alco::core::ITemperatureSensor {
public:
    DS18B20Sensor(std::shared_ptr<alco::core::IOneWireTempBus> bus,
                  alco::core::OneWireRom rom,
                  std::string alias = {});

    // ITemperatureSensor / ISensor
    std::string id() const override;
    alco::core::Status init() override;
    alco::core::Status read(alco::core::Reading& out) override;
    alco::core::Status readCelsius(float& out) override;

    // Kalibracja (offset w °C)
    void setOffset(float offsetC) { offsetC_ = offsetC; }
    float offset() const { return offsetC_; }

    // Cache TTL (ms); 0 = wyłącz cache
    void setCacheTtlMs(uint32_t ttl) { cacheTtlMs_ = ttl; }
    uint32_t cacheTtlMs() const { return cacheTtlMs_; }

private:
    std::shared_ptr<alco::core::IOneWireTempBus> bus_;
    alco::core::OneWireRom rom_;
    std::string alias_;

    // cache
    bool   hasCache_{false};
    float  lastC_{0.f};
    uint64_t lastTsMs_{0};
    uint32_t cacheTtlMs_{250}; // domyślnie 250ms – pozwala szybkim odczytom korzystać z bufora

    // kalibracja
    float offsetC_{0.f};

    // pomoce
    static inline uint64_t nowMs() { return esp_timer_get_time() / 1000ULL; }
};

} // namespace
