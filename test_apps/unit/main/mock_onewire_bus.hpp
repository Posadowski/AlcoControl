#pragma once
#include <string>
#include <vector>
#include "IOneWireTempBus.hpp"

// Fake 1-Wire bus: counts invocations and allows for the injection of errors.
class MockOneWireBus : public alco::core::IOneWireTempBus {
public:
    using Status = alco::core::Status;
    using OneWireRom = alco::core::OneWireRom;

    float  temperature = 20.0f;
    Status initResult  = Status::ok();
    Status startResult = Status::ok();
    Status readResult  = Status::ok();

    int initCalls = 0;
    int startCalls = 0;
    int readCalls = 0;
    std::string lastRom;

    Status init() override { ++initCalls; return initResult; }
    std::string name() const override { return "mock"; }

    Status search(std::vector<OneWireRom>& out) override {
        out.clear();
        return Status::ok();
    }

    Status startConversion(const OneWireRom& dev) override {
        ++startCalls;
        lastRom = dev.rom;
        return startResult;
    }

    Status readTemperature(const OneWireRom& dev, float& outCelsius) override {
        ++readCalls;
        lastRom = dev.rom;
        if (readResult.code != alco::core::StatusCode::Ok) return readResult;
        outCelsius = temperature;
        return Status::ok();
    }
};