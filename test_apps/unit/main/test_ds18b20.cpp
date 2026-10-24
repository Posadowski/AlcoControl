#include <memory>
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "DS18B20Sensor.hpp"
#include "mock_onewire_bus.hpp"

using namespace alco::core;
using namespace alco::ds18b20;

#define ASSERT_CODE(expected, st) \
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>((st).code))

namespace {
const char* kRom = "28-0123456789ABCD";

struct Fixture {
    std::shared_ptr<MockOneWireBus> bus = std::make_shared<MockOneWireBus>();
    DS18B20Sensor sensor;
    explicit Fixture(const char* alias = "boiler")
        : sensor(bus, OneWireRom{kRom}, alias) {}
};
}

// ---------- id / init ----------

TEST_CASE("DS18B20: id returns alias", "[ds18b20]")
{
    Fixture f("boiler");
    TEST_ASSERT_EQUAL_STRING("boiler", f.sensor.id().c_str());
}

TEST_CASE("DS18B20: id falls back to ROM when alias empty", "[ds18b20]")
{
    Fixture f("");
    TEST_ASSERT_EQUAL_STRING(kRom, f.sensor.id().c_str());
}

TEST_CASE("DS18B20: init delegates to bus and propagates error", "[ds18b20]")
{
    Fixture f;
    TEST_ASSERT_EQUAL_INT(StatusCode::Ok == f.sensor.init().code, 1);
    TEST_ASSERT_EQUAL_INT(1, f.bus->initCalls);

    f.bus->initResult = Status{StatusCode::BusError, "init"};
    ASSERT_CODE(StatusCode::BusError, f.sensor.init());
}

TEST_CASE("DS18B20: null bus gives InvalidState, no crash", "[ds18b20]")
{
    DS18B20Sensor s(nullptr, OneWireRom{kRom});
    float t = -1.0f;
    ASSERT_CODE(StatusCode::InvalidState, s.init());
    ASSERT_CODE(StatusCode::InvalidState, s.readCelsius(t));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, t);
}

// ---------- basic reading ----------

TEST_CASE("DS18B20: reads value, one conversion and one read", "[ds18b20]")
{
    Fixture f;
    f.bus->temperature = 21.5f;
    float t = 0;
    ASSERT_CODE(StatusCode::Ok, f.sensor.readCelsius(t));
    TEST_ASSERT_EQUAL_FLOAT(21.5f, t);
    TEST_ASSERT_EQUAL_INT(1, f.bus->startCalls);
    TEST_ASSERT_EQUAL_INT(1, f.bus->readCalls);
}

TEST_CASE("DS18B20: passes its ROM to the bus", "[ds18b20]")
{
    Fixture f;
    float t = 0;
    f.sensor.readCelsius(t);
    TEST_ASSERT_EQUAL_STRING(kRom, f.bus->lastRom.c_str());
}

TEST_CASE("DS18B20: applies calibration offset", "[ds18b20]")
{
    Fixture f;
    f.bus->temperature = 20.0f;
    f.sensor.setOffset(1.5f);
    float t = 0;
    ASSERT_CODE(StatusCode::Ok, f.sensor.readCelsius(t));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 21.5f, t);
}

// ---------- cache ----------

TEST_CASE("DS18B20: cache hit within TTL does not touch the bus", "[ds18b20][cache]")
{
    Fixture f;
    f.sensor.setCacheTtlMs(5000);
    f.bus->temperature = 20.0f;
    float t1 = 0, t2 = 0;
    f.sensor.readCelsius(t1);
    f.bus->temperature = 30.0f;          // a change on the bus should not be visible
    f.sensor.readCelsius(t2);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, t1);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, t2);
    TEST_ASSERT_EQUAL_INT(1, f.bus->startCalls);
}

TEST_CASE("DS18B20: cache expires after TTL", "[ds18b20][cache]")
{
    Fixture f;
    f.sensor.setCacheTtlMs(50);
    f.bus->temperature = 20.0f;
    float t = 0;
    f.sensor.readCelsius(t);
    f.bus->temperature = 30.0f;
    vTaskDelay(pdMS_TO_TICKS(120));
    f.sensor.readCelsius(t);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, t);
    TEST_ASSERT_EQUAL_INT(2, f.bus->startCalls);
}

TEST_CASE("DS18B20: TTL 0 disables cache", "[ds18b20][cache]")
{
    Fixture f;
    f.sensor.setCacheTtlMs(0);
    float t = 0;
    f.sensor.readCelsius(t);
    f.sensor.readCelsius(t);
    TEST_ASSERT_EQUAL_INT(2, f.bus->startCalls);
}

// ---------- errors ----------

TEST_CASE("DS18B20: startConversion error propagates, out untouched", "[ds18b20][error]")
{
    Fixture f;
    f.bus->startResult = Status{StatusCode::Timeout, "no presence"};
    float t = -1.0f;
    ASSERT_CODE(StatusCode::Timeout, f.sensor.readCelsius(t));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, t);
    TEST_ASSERT_EQUAL_INT(1, f.bus->startCalls);
    TEST_ASSERT_EQUAL_INT(0, f.bus->readCalls);   // no reading after a failed conversion
}

TEST_CASE("DS18B20: readTemperature error (CRC) propagates", "[ds18b20][error]")
{
    Fixture f;
    f.bus->readResult = Status{StatusCode::BusError, "crc"};
    float t = -1.0f;
    ASSERT_CODE(StatusCode::BusError, f.sensor.readCelsius(t));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, t);
}

TEST_CASE("DS18B20: error after expired cache does not serve stale value, then recovers",
          "[ds18b20][error][cache]")
{
    Fixture f;
    f.sensor.setCacheTtlMs(50);
    f.bus->temperature = 20.0f;
    float t = 0;
    f.sensor.readCelsius(t);                       // populates the cache

    vTaskDelay(pdMS_TO_TICKS(120));                // cache expires
    f.bus->readResult = Status{StatusCode::BusError, "crc"};
    t = -1.0f;
    ASSERT_CODE(StatusCode::BusError, f.sensor.readCelsius(t));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, t);             // did not return the old data 20.0

    f.bus->readResult = Status::ok();              // bus returns
    f.bus->temperature = 25.0f;
    ASSERT_CODE(StatusCode::Ok, f.sensor.readCelsius(t));
    TEST_ASSERT_EQUAL_FLOAT(25.0f, t);
}

// ---------- read() (ISensor) ----------

TEST_CASE("DS18B20: read() fills value and timestamp", "[ds18b20]")
{
    Fixture f;
    f.bus->temperature = 22.0f;
    Reading r{};
    const uint64_t before = esp_timer_get_time() / 1000ULL;
    ASSERT_CODE(StatusCode::Ok, f.sensor.read(r));
    const uint64_t after = esp_timer_get_time() / 1000ULL;
    TEST_ASSERT_EQUAL_FLOAT(22.0f, r.value);
    TEST_ASSERT_TRUE(r.ts.ms >= before && r.ts.ms <= after);
}

TEST_CASE("DS18B20: read() error leaves Reading untouched", "[ds18b20][error]")
{
    Fixture f;
    f.bus->startResult = Status{StatusCode::Timeout, "no presence"};
    Reading r{};
    r.value = -1.0f;
    ASSERT_CODE(StatusCode::Timeout, f.sensor.read(r));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, r.value);
}

// ---------- characterization (documents current behavior) ----------

TEST_CASE("DS18B20: offset change is not visible until cache expires", "[ds18b20][characterization]")
{
    // The offset is added BEFORE writing to the cache, so changing the offset
    // only takes effect after the TTL expires. If you change the implementation
    // (to cache the raw value), reverse this assertion.
    Fixture f;
    f.sensor.setCacheTtlMs(5000);
    f.bus->temperature = 20.0f;
    float t = 0;
    f.sensor.readCelsius(t);
    f.sensor.setOffset(5.0f);
    f.sensor.readCelsius(t);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, t);
}