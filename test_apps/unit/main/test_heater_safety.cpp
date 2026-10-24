#include <cmath>
#include "unity.h"
#include "heater_safety.hpp"

using namespace alco::heater;

namespace {
SafetyConfig cfg() {
    SafetyConfig c;
    c.minValidC = 0.0f;
    c.maxValidC = 100.0f;
    c.timeoutMs = 5000;
    return c;
}
#define ASSERT_FAULT(expected, hs) \
    TEST_ASSERT_EQUAL_INT((int)(expected), (int)(hs).fault())
}

TEST_CASE("Safety: not armed means no heating, no fault, readings ignored", "[heater_safety]")
{
    HeaterSafety hs(cfg());
    hs.onTemperature(NAN, 0);                       // ignored while disarmed
    TEST_ASSERT_FALSE(hs.check(0));
    ASSERT_FAULT(Fault::None, hs);
}

TEST_CASE("Safety: armed but no reading yet waits without a fault", "[heater_safety]")
{
    HeaterSafety hs(cfg());
    hs.arm(1000);
    TEST_ASSERT_FALSE(hs.check(1000));
    TEST_ASSERT_FALSE(hs.check(3000));
    ASSERT_FAULT(Fault::None, hs);
}

TEST_CASE("Safety: valid reading allows heating", "[heater_safety]")
{
    HeaterSafety hs(cfg());
    hs.arm(0);
    hs.onTemperature(40.0f, 100);
    TEST_ASSERT_TRUE(hs.check(200));
    TEST_ASSERT_EQUAL_FLOAT(40.0f, hs.lastTemperature());
}

TEST_CASE("Safety: no reading at all latches SensorTimeout", "[heater_safety]")
{
    HeaterSafety hs(cfg());
    hs.arm(0);
    TEST_ASSERT_FALSE(hs.check(5001));
    ASSERT_FAULT(Fault::SensorTimeout, hs);
}

TEST_CASE("Safety: stale reading latches SensorTimeout", "[heater_safety]")
{
    HeaterSafety hs(cfg());
    hs.arm(0);
    hs.onTemperature(40.0f, 1000);
    TEST_ASSERT_TRUE(hs.check(5000));
    TEST_ASSERT_FALSE(hs.check(6001));
    ASSERT_FAULT(Fault::SensorTimeout, hs);
}

TEST_CASE("Safety: exactly at the timeout is still allowed", "[heater_safety]")
{
    HeaterSafety hs(cfg());
    hs.arm(0);
    hs.onTemperature(40.0f, 1000);
    TEST_ASSERT_TRUE(hs.check(6000));               // age == timeoutMs
    ASSERT_FAULT(Fault::None, hs);
}

TEST_CASE("Safety: NaN latches InvalidReading and a good reading does not clear it",
          "[heater_safety][error]")
{
    HeaterSafety hs(cfg());
    hs.arm(0);
    hs.onTemperature(40.0f, 100);
    hs.onTemperature(NAN, 200);
    ASSERT_FAULT(Fault::InvalidReading, hs);
    TEST_ASSERT_FALSE(hs.check(250));
    hs.onTemperature(40.0f, 300);
    TEST_ASSERT_FALSE(hs.check(350));
    ASSERT_FAULT(Fault::InvalidReading, hs);
}

TEST_CASE("Safety: infinity is an invalid reading", "[heater_safety][error]")
{
    HeaterSafety hs(cfg());
    hs.arm(0);
    hs.onTemperature(INFINITY, 100);
    ASSERT_FAULT(Fault::InvalidReading, hs);
}

TEST_CASE("Safety: above max latches Overtemp, equal to max is fine", "[heater_safety]")
{
    HeaterSafety hs(cfg());
    hs.arm(0);
    hs.onTemperature(100.0f, 100);
    TEST_ASSERT_TRUE(hs.check(150));
    hs.onTemperature(100.1f, 200);
    ASSERT_FAULT(Fault::Overtemp, hs);
    TEST_ASSERT_FALSE(hs.check(250));
}

TEST_CASE("Safety: below min latches TooLow", "[heater_safety]")
{
    HeaterSafety hs(cfg());
    hs.arm(0);
    hs.onTemperature(-0.5f, 100);
    ASSERT_FAULT(Fault::TooLow, hs);
    TEST_ASSERT_FALSE(hs.check(150));
}

TEST_CASE("Safety: clear() resets the fault and disarms; re-arming works", "[heater_safety]")
{
    HeaterSafety hs(cfg());
    hs.arm(0);
    hs.onTemperature(150.0f, 100);
    ASSERT_FAULT(Fault::Overtemp, hs);

    hs.clear();
    ASSERT_FAULT(Fault::None, hs);
    TEST_ASSERT_FALSE(hs.check(200));               // disarmed

    hs.arm(300);
    hs.onTemperature(40.0f, 400);
    TEST_ASSERT_TRUE(hs.check(500));
}

TEST_CASE("Safety: toString covers every fault", "[heater_safety]")
{
    TEST_ASSERT_EQUAL_STRING("none", toString(Fault::None));
    TEST_ASSERT_EQUAL_STRING("sensor timeout", toString(Fault::SensorTimeout));
    TEST_ASSERT_EQUAL_STRING("overtemperature", toString(Fault::Overtemp));
}

TEST_CASE("Safety: disarm keeps the fault but ignores new readings", "[heater_safety]")
{
    HeaterSafety hs(cfg());
    hs.arm(0);
    hs.onTemperature(150.0f, 100);
    hs.disarm();
    ASSERT_FAULT(Fault::Overtemp, hs);
    hs.onTemperature(40.0f, 200);
    TEST_ASSERT_FALSE(hs.check(250));
    ASSERT_FAULT(Fault::Overtemp, hs);
}