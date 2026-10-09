#include <cmath>
#include "unity.h"
#include "pid_controller.hpp"

TEST_CASE("PID: clean P", "[pid]")
{
    PIDController pid(2.0f, 0.0f, 0.0f);
    pid.setTarget(50.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, pid.compute(40.0f, 1.0f)); // 2 * 10
}

TEST_CASE("PID: output limited to 0–100", "[pid]")
{
    PIDController pid(100.0f, 0.0f, 0.0f);
    pid.setTarget(100.0f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, pid.compute(0.0f, 1.0f));   // duży błąd +
    TEST_ASSERT_EQUAL_FLOAT(0.0f,   pid.compute(200.0f, 1.0f)); // błąd -
}

TEST_CASE("PID: The integral increases over time.", "[pid]")
{
    PIDController pid(0.0f, 1.0f, 0.0f);
    pid.setTarget(20.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, pid.compute(10.0f, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, pid.compute(10.0f, 1.0f));
}

TEST_CASE("PID: Anti-windup limits the integral.", "[pid]")
{
    PIDController pid(0.0f, 1.0f, 0.0f);
    pid.setTarget(100.0f);
    for (int i = 0; i < 1000; ++i) pid.compute(0.0f, 1.0f); // długo za zimno

    // po przekroczeniu celu całka (max 100) musi dać się "rozładować"
    pid.setTarget(0.0f);
    float out = 100.0f;
    for (int i = 0; i < 5; ++i) out = pid.compute(100.0f, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out);
}

TEST_CASE("PID: A reset clears the state", "[pid]")
{
    PIDController pid(0.0f, 1.0f, 0.0f);
    pid.setTarget(20.0f);
    pid.compute(10.0f, 1.0f);
    pid.reset();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, pid.compute(10.0f, 1.0f));
}

TEST_CASE("PID: dt = 0 cannot deliver full power", "[pid][bug]")
{
    PIDController pid(1.0f, 0.0f, 1.0f);
    pid.setTarget(50.0f);
    pid.compute(50.0f, 1.0f);          // błąd 0
    float out = pid.compute(50.0f, 0.0f); // dt = 0
    TEST_ASSERT_FALSE(std::isnan(out));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, out);
}

TEST_CASE("PID: NaN turns off the heater", "[pid][bug]")
{
    PIDController pid(2.0f, 0.1f, 0.5f);
    pid.setTarget(50.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.compute(NAN, 1.0f));
}