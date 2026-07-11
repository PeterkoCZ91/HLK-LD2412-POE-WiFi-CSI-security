// Native unit tests for the CSI shadow detector (P1.1, navrh 17.1).
// Pure — no Arduino. Run: pio test -e native -f test_csi_shadow
#include <unity.h>
#include "services/CsiShadowDetector.h"

void setUp() {}
void tearDown() {}

// Feed a constant variance for n ticks; return final shadow motion.
static bool feed(CsiShadowDetector& d, float var, float thr, float hys, int n) {
    bool m = false;
    for (int i = 0; i < n; i++) m = d.update(var, thr, hys);
    return m;
}

void test_starts_idle() {
    CsiShadowDetector d;
    TEST_ASSERT_FALSE(d.motion());
}

void test_high_variance_enters_after_enough_votes() {
    CsiShadowDetector d;  // enter=4
    // variance well above threshold — takes 4 ticks (4/6) to enter
    TEST_ASSERT_FALSE(d.update(1.0f, 0.1f, 0.7f));   // 1 vote
    TEST_ASSERT_FALSE(d.update(1.0f, 0.1f, 0.7f));   // 2
    TEST_ASSERT_FALSE(d.update(1.0f, 0.1f, 0.7f));   // 3
    TEST_ASSERT_TRUE (d.update(1.0f, 0.1f, 0.7f));   // 4 → enter
}

void test_low_variance_stays_idle() {
    CsiShadowDetector d;
    TEST_ASSERT_FALSE(feed(d, 0.01f, 0.1f, 0.7f, 10));
}

void test_hysteresis_holds_motion_between_thr_and_exit() {
    CsiShadowDetector d;
    feed(d, 1.0f, 0.1f, 0.7f, 6);            // firmly in motion
    TEST_ASSERT_TRUE(d.motion());
    // variance drops to 0.08 — below threshold 0.1 but above exit level 0.07:
    // rawMotion stays true (>= thr*hys) so motion persists
    TEST_ASSERT_TRUE(feed(d, 0.08f, 0.1f, 0.7f, 6));
}

void test_variance_below_exit_level_exits() {
    CsiShadowDetector d;
    feed(d, 1.0f, 0.1f, 0.7f, 6);            // in motion
    // variance drops below exit level (0.07) → raw idle → exits after 5/6 idle votes
    TEST_ASSERT_FALSE(feed(d, 0.0f, 0.1f, 0.7f, 6));
}

void test_reset_clears_state() {
    CsiShadowDetector d;
    feed(d, 1.0f, 0.1f, 0.7f, 6);
    TEST_ASSERT_TRUE(d.motion());
    d.reset();
    TEST_ASSERT_FALSE(d.motion());
}

void test_candidate_threshold_changes_verdict() {
    // same variance signal, two thresholds → different shadow verdicts.
    // This is the whole point of shadow eval: "would this threshold fire?"
    CsiShadowDetector loose;   // low threshold → fires
    CsiShadowDetector tight;   // high threshold → stays quiet
    bool loudFires = feed(loose, 0.2f, 0.05f, 0.7f, 6);
    bool tightFires = feed(tight, 0.2f, 0.5f, 0.7f, 6);
    TEST_ASSERT_TRUE(loudFires);
    TEST_ASSERT_FALSE(tightFires);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_idle);
    RUN_TEST(test_high_variance_enters_after_enough_votes);
    RUN_TEST(test_low_variance_stays_idle);
    RUN_TEST(test_hysteresis_holds_motion_between_thr_and_exit);
    RUN_TEST(test_variance_below_exit_level_exits);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_candidate_threshold_changes_verdict);
    return UNITY_END();
}
