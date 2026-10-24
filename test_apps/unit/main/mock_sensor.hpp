#pragma once
#include <atomic>
#include <memory>
#include <string>
#include "ISensor.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Stan współdzielony z testem (SensorManager przejmuje własność sensora,
// więc liczniki muszą leżeć poza nim).
struct MockSensorState {
    std::atomic<int>      initCalls{0};
    std::atomic<int>      readCalls{0};
    std::atomic<float>    value{20.0f};
    std::atomic<bool>     failRead{false};
    std::atomic<uint32_t> readDelayMs{0};
};

class MockSensor : public alco::core::ISensor {
public:
    MockSensor(std::string id, std::shared_ptr<MockSensorState> st)
        : id_(std::move(id)), st_(std::move(st)) {}

    std::string id() const override { return id_; }

    alco::core::Status init() override {
        ++st_->initCalls;
        return alco::core::Status::ok();
    }

    alco::core::Status read(alco::core::Reading& out) override {
        ++st_->readCalls;
        const uint32_t d = st_->readDelayMs;
        if (d) vTaskDelay(pdMS_TO_TICKS(d));
        if (st_->failRead) return {alco::core::StatusCode::BusError, "mock"};
        out.value = st_->value;
        out.ts.ms = 0;
        return alco::core::Status::ok();
    }

private:
    std::string id_;
    std::shared_ptr<MockSensorState> st_;
};