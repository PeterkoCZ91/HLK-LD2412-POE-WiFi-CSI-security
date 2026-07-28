// Native unit tests for the ML saturation guard (pure, no Arduino).
// Run: pio test -e native -f test_ml_saturation
#include <unity.h>
#include "services/CsiMlSaturation.h"

void setUp() {}
void tearDown() {}

// Feed `minutes` minut po `ticksPerMin` ticích s daným duty (0-100 %).
static void feedMinutes(CsiMlSaturationGuard& g, uint32_t& nowMs,
                        uint32_t minutes, uint8_t dutyPct,
                        uint32_t ticksPerMin = 60) {
    for (uint32_t m = 0; m < minutes; m++) {
        for (uint32_t t = 0; t < ticksPerMin; t++) {
            bool motion = (t * 100u < (uint32_t)dutyPct * ticksPerMin);
            g.tick(motion, nowMs);
            nowMs += 60000u / ticksPerMin;
        }
    }
}

void test_fresh_guard_is_trusted() {
    CsiMlSaturationGuard g;
    TEST_ASSERT_FALSE(g.saturated());
}

void test_saturates_after_full_window_at_100pct() {
    CsiMlSaturationGuard g;
    uint32_t now = 1000;
    feedMinutes(g, now, CsiMlSaturationGuard::WINDOW_MIN, 100);
    // okno plné samých true → bucket navíc, aby proběhlo vyhodnocení
    feedMinutes(g, now, 1, 100);
    TEST_ASSERT_TRUE(g.saturated());
}

void test_no_saturation_below_threshold_duty() {
    CsiMlSaturationGuard g;
    uint32_t now = 1000;
    feedMinutes(g, now, CsiMlSaturationGuard::WINDOW_MIN + 5, 90);  // < 95 %
    TEST_ASSERT_FALSE(g.saturated());
}

void test_no_saturation_before_window_filled() {
    CsiMlSaturationGuard g;
    uint32_t now = 1000;
    feedMinutes(g, now, CsiMlSaturationGuard::WINDOW_MIN / 2, 100);
    TEST_ASSERT_FALSE(g.saturated());
}

void test_recovers_when_duty_drops() {
    CsiMlSaturationGuard g;
    uint32_t now = 1000;
    feedMinutes(g, now, CsiMlSaturationGuard::WINDOW_MIN + 1, 100);
    TEST_ASSERT_TRUE(g.saturated());
    // hodina normálního chování (10 % duty) → návrat důvěry
    feedMinutes(g, now, CsiMlSaturationGuard::RECOVER_MIN + 1, 10);
    TEST_ASSERT_FALSE(g.saturated());
}

void test_stays_saturated_while_duty_high() {
    CsiMlSaturationGuard g;
    uint32_t now = 1000;
    feedMinutes(g, now, CsiMlSaturationGuard::WINDOW_MIN + 1, 100);
    feedMinutes(g, now, CsiMlSaturationGuard::RECOVER_MIN + 1, 80);  // > 50 %
    TEST_ASSERT_TRUE(g.saturated());
}

void test_gap_in_feed_does_not_crash_or_fake_buckets() {
    CsiMlSaturationGuard g;
    uint32_t now = 1000;
    feedMinutes(g, now, 10, 100);
    now += 3600000;  // hodina bez CSI datového toku (starved link)
    feedMinutes(g, now, 10, 100);
    TEST_ASSERT_FALSE(g.saturated());  // jen 20 reálných minut, okno neplné
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fresh_guard_is_trusted);
    RUN_TEST(test_saturates_after_full_window_at_100pct);
    RUN_TEST(test_no_saturation_below_threshold_duty);
    RUN_TEST(test_no_saturation_before_window_filled);
    RUN_TEST(test_recovers_when_duty_drops);
    RUN_TEST(test_stays_saturated_while_duty_high);
    RUN_TEST(test_gap_in_feed_does_not_crash_or_fake_buckets);
    return UNITY_END();
}
