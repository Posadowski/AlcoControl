#include <cmath>
#include <vector>
#include "unity.h"
#include "burst_scheduler.hpp"

using alco::heater::BurstScheduler;

namespace {
int countFires(BurstScheduler& s, int n) {
    int c = 0;
    for (int i = 0; i < n; ++i) if (s.nextHalfCycle()) ++c;
    return c;
}
}

TEST_CASE("Burst: 0% never fires", "[burst]")
{
    BurstScheduler s;
    s.setPower(0.0f);
    TEST_ASSERT_EQUAL_INT(0, countFires(s, 1000));
}

TEST_CASE("Burst: 100% always fires", "[burst]")
{
    BurstScheduler s;
    s.setPower(100.0f);
    TEST_ASSERT_EQUAL_INT(1000, countFires(s, 1000));
}

TEST_CASE("Burst: 30% fires exactly 30 times per window", "[burst]")
{
    BurstScheduler s;
    s.setPower(30.0f);
    TEST_ASSERT_EQUAL_INT(30, countFires(s, 100));
    TEST_ASSERT_EQUAL_INT(30, countFires(s, 100));
}

TEST_CASE("Burst: 25% is spread evenly, no adjacent fires", "[burst]")
{
    BurstScheduler s;
    s.setPower(25.0f);
    bool prev = false;
    int fires = 0;
    for (int i = 0; i < 100; ++i) {
        const bool f = s.nextHalfCycle();
        TEST_ASSERT_FALSE(f && prev);
        prev = f;
        if (f) ++fires;
    }
    TEST_ASSERT_EQUAL_INT(25, fires);
}

TEST_CASE("Burst: 10% fires every 10th half-cycle", "[burst]")
{
    BurstScheduler s;
    s.setPower(10.0f);
    std::vector<int> idx;
    for (int i = 0; i < 100; ++i) if (s.nextHalfCycle()) idx.push_back(i);
    TEST_ASSERT_EQUAL_INT(10, (int)idx.size());
    for (size_t i = 1; i < idx.size(); ++i) TEST_ASSERT_EQUAL_INT(10, idx[i] - idx[i - 1]);
}

TEST_CASE("Burst: invalid input is clamped, NaN means off", "[burst][error]")
{
    BurstScheduler s;
    s.setPower(NAN);
    TEST_ASSERT_EQUAL_INT(0, countFires(s, 100));
    s.setPower(-5.0f);
    TEST_ASSERT_EQUAL_INT(0, countFires(s, 100));
    s.setPower(150.0f);
    TEST_ASSERT_EQUAL_INT(100, countFires(s, 100));
}

TEST_CASE("Burst: resolution is 1% for a 100-cycle window", "[burst]")
{
    BurstScheduler s;
    s.setPower(0.4f);
    TEST_ASSERT_EQUAL_INT(0, countFires(s, 100));
    s.setPower(0.6f);
    TEST_ASSERT_EQUAL_INT(1, countFires(s, 100));
}

TEST_CASE("Burst: changing power mid-run gives the exact new count in the next window",
          "[burst]")
{
    BurstScheduler s;
    s.setPower(80.0f);
    countFires(s, 37);              // stop in the middle of a window
    s.setPower(20.0f);
    TEST_ASSERT_EQUAL_INT(20, countFires(s, 100));
    s.setPower(55.0f);
    TEST_ASSERT_EQUAL_INT(55, countFires(s, 100));
}

TEST_CASE("Burst: reset restarts the pattern", "[burst]")
{
    BurstScheduler a, b;
    a.setPower(33.0f);
    b.setPower(33.0f);
    countFires(a, 17);
    a.reset();
    for (int i = 0; i < 100; ++i) TEST_ASSERT_EQUAL(b.nextHalfCycle(), a.nextHalfCycle());
}

TEST_CASE("Burst: custom window", "[burst]")
{
    BurstScheduler s(10);
    s.setPower(50.0f);
    TEST_ASSERT_EQUAL_INT(5, countFires(s, 10));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, s.power());
}