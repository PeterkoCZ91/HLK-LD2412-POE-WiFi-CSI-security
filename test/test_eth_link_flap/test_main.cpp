// Native unit tests for ETH link flap accounting.
// Run: pio test -e native -f test_eth_link_flap
//
// Field context (z15 node, 2026-08-22): the IDF ethernet driver polls the PHY
// every 2000 ms, so every real link loss is reported as a whole multiple of
// 2 s. Both reporting layers above it aliased that badly — the 60 s
// connectivity watchdog logged "restored after 60s" for a 2 s glitch, and the
// 30 s MQTT diagnostics cadence made Home Assistant see 339 episodes/day when
// the driver was actually firing ~6800. These tests pin the accounting that
// replaces both guesses with a measurement.
#include <unity.h>
#include "services/EthLinkFlapTracker.h"

void setUp() {}
void tearDown() {}

void test_fresh_tracker_is_quiet() {
    EthLinkFlapTracker t;
    t.begin(1000);
    EthLinkFlapStats s = t.stats(61000);
    TEST_ASSERT_EQUAL_UINT32(0, s.downCount);
    TEST_ASSERT_EQUAL_UINT32(0, s.downTotalMs);
    TEST_ASSERT_EQUAL_UINT32(0, s.currentDownMs);
    TEST_ASSERT_EQUAL_UINT16(0, s.downPermille);
    TEST_ASSERT_FALSE(s.linkDown);
    TEST_ASSERT_EQUAL_UINT32(60000, s.sinceMs);
}

void test_one_episode_counted_and_measured() {
    EthLinkFlapTracker t;
    t.begin(0);
    t.onLinkDown(10000);
    t.onLinkUp(14000);                      // 4 s outage, the common field length
    EthLinkFlapStats s = t.stats(20000);
    TEST_ASSERT_EQUAL_UINT32(1, s.downCount);
    TEST_ASSERT_EQUAL_UINT32(4000, s.downTotalMs);
    TEST_ASSERT_EQUAL_UINT32(4000, s.longestDownMs);
    TEST_ASSERT_EQUAL_UINT32(0, s.currentDownMs);
    TEST_ASSERT_FALSE(s.linkDown);
}

void test_in_progress_episode_is_visible_immediately() {
    // A node polled mid-outage must not look healthy: the running episode
    // counts toward the totals before the link comes back.
    EthLinkFlapTracker t;
    t.begin(0);
    t.onLinkDown(10000);
    EthLinkFlapStats s = t.stats(13000);
    TEST_ASSERT_TRUE(s.linkDown);
    TEST_ASSERT_EQUAL_UINT32(1, s.downCount);
    TEST_ASSERT_EQUAL_UINT32(3000, s.currentDownMs);
    TEST_ASSERT_EQUAL_UINT32(3000, s.downTotalMs);
    TEST_ASSERT_EQUAL_UINT32(3000, s.longestDownMs);
}

void test_repeated_down_events_do_not_double_count() {
    // The driver re-posts DISCONNECTED on every 2 s poll while the link stays
    // down; only the edge is an episode.
    EthLinkFlapTracker t;
    t.begin(0);
    t.onLinkDown(10000);
    t.onLinkDown(12000);
    t.onLinkDown(14000);
    t.onLinkUp(16000);
    EthLinkFlapStats s = t.stats(20000);
    TEST_ASSERT_EQUAL_UINT32(1, s.downCount);
    TEST_ASSERT_EQUAL_UINT32(6000, s.downTotalMs);
}

void test_up_without_down_is_ignored() {
    EthLinkFlapTracker t;
    t.begin(0);
    t.onLinkUp(5000);
    t.onLinkUp(9000);
    EthLinkFlapStats s = t.stats(10000);
    TEST_ASSERT_EQUAL_UINT32(0, s.downCount);
    TEST_ASSERT_EQUAL_UINT32(0, s.downTotalMs);
    TEST_ASSERT_FALSE(s.linkDown);
}

void test_longest_episode_survives_shorter_ones() {
    EthLinkFlapTracker t;
    t.begin(0);
    t.onLinkDown(1000);  t.onLinkUp(7000);   // 6 s
    t.onLinkDown(10000); t.onLinkUp(12000);  // 2 s
    EthLinkFlapStats s = t.stats(20000);
    TEST_ASSERT_EQUAL_UINT32(2, s.downCount);
    TEST_ASSERT_EQUAL_UINT32(8000, s.downTotalMs);
    TEST_ASSERT_EQUAL_UINT32(6000, s.longestDownMs);
}

void test_field_fingerprint_230s_window() {
    // Verbatim from the DBG ring, 2026-08-22 03:1x, 230 s span: 18 episodes,
    // 54.0 s down => 23.5 % of the time. This is the number that tells Petr
    // whether swapping the cable/port/PoE injector actually fixed anything.
    const uint32_t durations[18] = {2000, 2000, 2000, 4000, 4000, 2000,
                                    2000, 4000, 6000, 2000, 2000, 2000,
                                    4000, 2000, 6000, 2000, 2000, 4000};
    EthLinkFlapTracker t;
    t.begin(0);
    uint32_t now = 0;
    for (int i = 0; i < 18; i++) {
        now += 6000;                        // idle gap between episodes
        t.onLinkDown(now);
        now += durations[i];
        t.onLinkUp(now);
    }
    EthLinkFlapStats s = t.stats(230000);
    TEST_ASSERT_EQUAL_UINT32(18, s.downCount);
    TEST_ASSERT_EQUAL_UINT32(54000, s.downTotalMs);
    TEST_ASSERT_EQUAL_UINT32(6000, s.longestDownMs);
    TEST_ASSERT_EQUAL_UINT16(234, s.downPermille);   // 54000/230000
}

void test_permille_is_zero_before_any_time_passes() {
    EthLinkFlapTracker t;
    t.begin(5000);
    EthLinkFlapStats s = t.stats(5000);     // no elapsed window -> no divide by zero
    TEST_ASSERT_EQUAL_UINT16(0, s.downPermille);
    TEST_ASSERT_EQUAL_UINT32(0, s.sinceMs);
}

void test_millis_rollover_is_survived() {
    // millis() wraps every ~49.7 days; unsigned subtraction must carry us over.
    const uint32_t nearMax = 0xFFFFF000u;
    EthLinkFlapTracker t;
    t.begin(nearMax);
    t.onLinkDown(nearMax + 0x800);
    t.onLinkUp(nearMax + 0x800 + 4000);     // wraps past 0
    EthLinkFlapStats s = t.stats(nearMax + 0x800 + 10000);
    TEST_ASSERT_EQUAL_UINT32(1, s.downCount);
    TEST_ASSERT_EQUAL_UINT32(4000, s.downTotalMs);
    TEST_ASSERT_FALSE(s.linkDown);
}

void test_reset_clears_history_and_window() {
    EthLinkFlapTracker t;
    t.begin(0);
    t.onLinkDown(1000); t.onLinkUp(3000);
    t.reset(50000);
    EthLinkFlapStats s = t.stats(60000);
    TEST_ASSERT_EQUAL_UINT32(0, s.downCount);
    TEST_ASSERT_EQUAL_UINT32(0, s.downTotalMs);
    TEST_ASSERT_EQUAL_UINT32(0, s.longestDownMs);
    TEST_ASSERT_EQUAL_UINT32(10000, s.sinceMs);
}

void test_reset_while_down_keeps_link_state() {
    // Reset is a counter zeroing, not a claim that the link came back.
    EthLinkFlapTracker t;
    t.begin(0);
    t.onLinkDown(1000);
    t.reset(2000);
    EthLinkFlapStats s = t.stats(5000);
    TEST_ASSERT_TRUE(s.linkDown);
    TEST_ASSERT_EQUAL_UINT32(3000, s.currentDownMs);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fresh_tracker_is_quiet);
    RUN_TEST(test_one_episode_counted_and_measured);
    RUN_TEST(test_in_progress_episode_is_visible_immediately);
    RUN_TEST(test_repeated_down_events_do_not_double_count);
    RUN_TEST(test_up_without_down_is_ignored);
    RUN_TEST(test_longest_episode_survives_shorter_ones);
    RUN_TEST(test_field_fingerprint_230s_window);
    RUN_TEST(test_permille_is_zero_before_any_time_passes);
    RUN_TEST(test_millis_rollover_is_survived);
    RUN_TEST(test_reset_clears_history_and_window);
    RUN_TEST(test_reset_while_down_keeps_link_state);
    return UNITY_END();
}
