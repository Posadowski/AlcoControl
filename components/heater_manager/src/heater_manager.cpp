#include "heater_manager.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>

static const char* TAG = "HEATER_MGR";

// Parametry PID - możesz je dostroić
#define PID_KP 2.0f     // Proporcjonalny
#define PID_KI 0.1f     // Całkujący  
#define PID_KD 0.5f     // Różniczkujący

#define CONTROL_PERIOD_MS 1000  // Okres regulacji co 1 sekundę
#define MAX_TEMP_DIFF 5.0f      // Maksymalna różnica temperatury przed awarią
#define MIN_TEMP_LIMIT 10.0f    // Minimalna temperatura (wykrycie odłączenia sensora)
#define MAX_TEMP_LIMIT 120.0f   // Maksymalna temperatura (bezpieczeństwo)

HeaterManager::HeaterManager() 
    : zero_cross()
    , triac()
    , pid(PID_KP, PID_KI, PID_KD)
    , current_temperature(25.0f)
    , target_temperature(PRODUCTION_TEMPERATURE_C)
    , current_power(0.0f)
    , running(false)
    , control_task_handle(nullptr) {
}

void HeaterManager::init() {
    ESP_LOGI(TAG, "Initializing heater manager...");
    
    // Inicjalizacja komponentów
    zero_cross.init();
    triac.init();
    
    // Ustawienia PID
    pid.setTarget(target_temperature);
    
    ESP_LOGI(TAG, "Heater manager initialized. Target: %.1f°C", target_temperature);
}

void HeaterManager::setTargetTemperature(float target) {
    if (target < MIN_TEMP_LIMIT || target > MAX_TEMP_LIMIT) {
        ESP_LOGW(TAG, "Target temperature %.1f°C out of range (%.1f - %.1f)", 
                 target, MIN_TEMP_LIMIT, MAX_TEMP_LIMIT);
        return;
    }
    
    target_temperature = target;
    pid.setTarget(target);
    ESP_LOGI(TAG, "Target temperature set to %.1f°C", target);
}

void HeaterManager::updateTemperature(float temp) {
    // Walidacja temperatury
    if (temp < MIN_TEMP_LIMIT || temp > MAX_TEMP_LIMIT) {
        ESP_LOGW(TAG, "Temperature reading %.1f°C out of range, stopping heater", temp);
        stop();
        return;
    }
    
    // Sprawdź czy temperatura nie zmienia się zbyt szybko (może błąd sensora)
    if (fabsf(temp - current_temperature) > MAX_TEMP_DIFF && current_temperature > MIN_TEMP_LIMIT) {
        ESP_LOGW(TAG, "Temperature change too rapid: %.1f -> %.1f°C", 
                 current_temperature, temp);
        // Nie zatrzymuj od razu, ale zaloguj ostrzeżenie
    }
    
    current_temperature = temp;
    ESP_LOGD(TAG, "Temperature updated: %.1f°C", temp);
}

void HeaterManager::start() {
    if (running) {
        ESP_LOGW(TAG, "Heater already running");
        return;
    }
    
    ESP_LOGI(TAG, "Starting heater control...");
    running = true;
    pid.reset();
    
    // Utwórz task sterowania
    xTaskCreatePinnedToCore(
        controlTask,
        "heater_control",
        4096,  // Stack size
        this,  // Parameter
        5,     // Priority (wysoka dla kontroli grzałki)
        &control_task_handle,
        1      // Core 1 (Core 0 dla WiFi)
    );
    
    ESP_LOGI(TAG, "Heater control started");
}

void HeaterManager::stop() {
    if (!running) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping heater control...");
    running = false;
    
    // Wyłącz triak
    triac.turnOff();
    current_power = 0.0f;
    
    // Usuń task jeśli istnieje
    if (control_task_handle) {
        vTaskDelete(control_task_handle);
        control_task_handle = nullptr;
    }
    
    ESP_LOGI(TAG, "Heater control stopped");
}

bool HeaterManager::isRunning() {
    return running;
}

float HeaterManager::getCurrentPower() {
    return current_power;
}

void HeaterManager::controlTask(void* pvParameter) {
    HeaterManager* heater = static_cast<HeaterManager*>(pvParameter);
    heater->controlLoop();
}

void HeaterManager::controlLoop() {
    ESP_LOGI(TAG, "Control loop started");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    uint64_t last_time_us = esp_timer_get_time();
    
    while (running) {
        uint64_t current_time_us = esp_timer_get_time();
        float dt = (current_time_us - last_time_us) / 1000000.0f; // Delta time w sekundach
        last_time_us = current_time_us;
        
        // Oblicz wyjście PID
        float pid_output = pid.compute(current_temperature, dt);
        current_power = pid_output;
        
        // Ustaw moc triaka
        triac.setPowerPercent(current_power);
        
        // Logowanie co 10 sekund
        static int log_counter = 0;
        if (++log_counter >= 10) {
            log_counter = 0;
            float error = target_temperature - current_temperature;
            ESP_LOGI(TAG, "T: %.1f°C | Target: %.1f°C | Error: %.1f°C | Power: %.1f%%", 
                     current_temperature, target_temperature, error, current_power);
        }
        
        // Sterowanie w cyklu zero-cross
        controlZeroCross();
        
        // Czekaj na następny cykl sterowania
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
    
    ESP_LOGI(TAG, "Control loop ended");
    control_task_handle = nullptr;
    vTaskDelete(nullptr);
}

void HeaterManager::controlZeroCross() {
    // Jeśli moc = 0, nie włączaj triaka
    if (current_power <= 0.1f) {
        return;
    }
    
    // Czekaj na zero cross przez maksymalnie 12ms (więcej niż pół cyklu 50Hz)
    if (!zero_cross.waitForZeroCross(12)) {
        ESP_LOGW(TAG, "Zero cross timeout - AC power issue?");
        return;
    }
    
    // Oblicz opóźnienie na podstawie mocy
    uint32_t delay_us = calculateDelayFromPower(current_power);
    
    // Wyzwól triak z odpowiednim opóźnieniem
    if (delay_us < 9500) { // Bezpieczny margines przed końcem półcyklu
        triac.triggerAtDelay(delay_us);
    }
}

uint32_t HeaterManager::calculateDelayFromPower(float power_percent) {
    if (power_percent <= 0.0f) return 10000; // Pełny półcykl = wyłączone
    if (power_percent >= 100.0f) return 100;  // Minimalne opóźnienie
    
    // Konwersja procent mocy na kąt fazowy (0-180°)
    // Dla regulacji fazowej: mała moc = duże opóźnienie
    float normalized_power = power_percent / 100.0f;
    float phase_angle = (1.0f - normalized_power) * 180.0f;
    float phase_radians = phase_angle * M_PI / 180.0f;
    
    // Opóźnienie w mikrosekundach (10ms = półcykl dla 50Hz)
    uint32_t delay_us = (uint32_t)(phase_radians * 10000.0f / M_PI);
    
    return delay_us;
}

// Funkcje pomocnicze do dostrajania PID przez HTTP
void HeaterManager::setPIDTunings(float kp, float ki, float kd) {
    pid.setTunings(kp, ki, kd);
    ESP_LOGI(TAG, "PID tunings updated: Kp=%.2f, Ki=%.2f, Kd=%.2f", kp, ki, kd);
}

void HeaterManager::getPIDTunings(float* kp, float* ki, float* kd) {
    // Te wartości można by przechować w klasie PIDController
    // Na razie zwracamy stałe
    *kp = PID_KP;
    *ki = PID_KI;  
    *kd = PID_KD;
}