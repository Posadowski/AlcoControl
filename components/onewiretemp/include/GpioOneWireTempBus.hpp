#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

#include "IOneWireTempBus.hpp"

extern "C" {
#include "driver/gpio.h"
}

namespace alco::onewire {

// Prosty 1-Wire po GPIO (bit-banging). Wymaga zewnętrznego podciągania (4.7k).
class GpioOneWireTempBus : public alco::core::IOneWireTempBus {
public:
    // resolutionBits: 9..12 (wpływa na czas konwersji)
    explicit GpioOneWireTempBus(gpio_num_t pin,
                                uint8_t resolutionBits = 12,
                                bool parasitePower = false,
                                std::string name = "1w-gpio");

    // IOneWireTempBus / ISensorBus
    alco::core::Status init() override;
    std::string name() const override;

    // Skanuje magistralę — zwraca listę ROMów
    alco::core::Status search(std::vector<alco::core::OneWireRom>& out) override;

    // Rozpocznij konwersję temperatury dla konkretnego urządzenia
    alco::core::Status startConversion(const alco::core::OneWireRom& dev) override;

    // Odczytaj temperaturę (°C) – funkcja robi MATCH ROM + READ SCRATCHPAD
    alco::core::Status readTemperature(const alco::core::OneWireRom& dev, float& outCelsius) override;

private:
    gpio_num_t pin_;
    uint8_t resBits_;
    bool parasite_;
    std::string name_;

    // --- prymitywy 1-Wire ---
    bool resetPresence();                  // reset + presence
    void writeBit(int bit);
    int  readBit();
    void writeByte(uint8_t v);
    uint8_t readByte();

    // ROM level
    void matchRom(const std::string& rom);
    void skipRom();

    // scratchpad / DS18B20
    bool crcOk(const uint8_t* data, size_t len);
    uint8_t crc8Dallas(const uint8_t* data, size_t len);

    // search algorithm state (Dallas 1-Wire Search)
    bool searchNext(std::string& rom, int& lastDiscrepancy, bool& lastDeviceFlag);

    // gpio helpers (open-drain symulowany: LOW = wyjście, HIGH-Z = wejście)
    inline void driveLow();
    inline void releaseBus();
    inline int  readPin();

    // timing
    void delayUs(uint32_t us);
    uint32_t conversionTimeUs() const; // zależnie od rozdzielczości
};

} // namespace
