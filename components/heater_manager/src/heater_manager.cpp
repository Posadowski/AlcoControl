#include "heater_manager.hpp"
#include "esp_log.h"
#include "esp_timer.h"

using alco::heater::Fault;
using alco::heater::SafetyConfig;

static const char* TAG = "HEATER_MGR";

static constexpr float PID_KP = 2.0f;
static constexpr float PID_KI = 0.02f;
static constexpr float PID_KD = 0.5f;
static constexpr uint32_t CONTROL_PERIOD_MS = 1000;

static SafetyConfig makeSafety() {
    SafetyConfig c;
    c.minValidC = -20.0f;
    c.maxValidC = 105.0f;     // hard cut-off; adjust to your process
    c.timeoutMs = 5000;       // no valid reading for 5 s -> heater off
    return c;
}

static uint64_t nowMs() { return esp_timer_get_time() / 1000ULL; }

HeaterManager::HeaterManager()
: controller(triac, PID_KP, PID_KI, PID_KD, makeSafety()) {}

void HeaterManager::init() {
    if (task_) return;
    triac.init();
    controller.setTarget(PRODUCTION_TEMPERATURE_C);
    zero_cross.init(&HeaterManager::onZeroCross, this);
    xTaskCreatePinnedToCore(&HeaterManager::controlTask, "heater_control", 4096, this,
                            5, &task_, 1);
    ESP_LOGI(TAG, "Heater manager initialized. Target: %.1f C", PRODUCTION_TEMPERATURE_C);
}

void HeaterManager::setTargetTemperature(float target) {
    if (!controller.setTarget(target)) ESP_LOGE(TAG, "Rejected target %.1f C", target);
}

void HeaterManager::updateTemperature(float t) { controller.onTemperature(t, nowMs()); }
void HeaterManager::start()                    { controller.start(nowMs()); }
void HeaterManager::stop()                     { controller.stop(); }
bool HeaterManager::isRunning()                { return controller.running(); }
float HeaterManager::getCurrentPower()         { return controller.power(); }
const char* HeaterManager::faultText()         { return toString(controller.fault()); }

void HeaterManager::setPIDTunings(float kp, float ki, float kd) {
    if (!controller.setTunings(kp, ki, kd)) ESP_LOGE(TAG, "Rejected PID tunings");
}
void HeaterManager::getPIDTunings(float* kp, float* ki, float* kd) {
    controller.getTunings(*kp, *ki, *kd);
}

void HeaterManager::onZeroCross(void* arg) {
    static_cast<HeaterManager*>(arg)->controller.onHalfCycle();
}

void HeaterManager::controlTask(void* arg) {
    static_cast<HeaterManager*>(arg)->controlLoop();
}

void HeaterManager::controlLoop() {
    TickType_t last = xTaskGetTickCount();
    int64_t lastUs = esp_timer_get_time();
    Fault lastFault = Fault::None;

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
        const int64_t nowUs = esp_timer_get_time();
        const float dt = static_cast<float>(nowUs - lastUs) / 1e6f;
        lastUs = nowUs;

        controller.step(static_cast<uint64_t>(nowUs / 1000), dt);

        // Your sdkconfig has log level ERROR, so faults must be logged with ESP_LOGE.
        const Fault f = controller.fault();
        if (f != lastFault) {
            if (f != Fault::None) ESP_LOGE(TAG, "Heater stopped: %s", toString(f));
            lastFault = f;
        }
    }
}