// Native unit tests for CSI decision-trace classification (P1.2, navrh 17.3).
// Pure — no Arduino. Run: pio test -e native -f test_csi_decision
#include <unity.h>
#include <string.h>
#include "services/CsiDecisionTrace.h"

void setUp() {}
void tearDown() {}

// ---- classifier: dominant reason -------------------------------------------
void test_buffer_not_ready_is_insufficient_samples() {
    // bufferReady=false dominates every other input
    TEST_ASSERT_EQUAL(CsiDecisionReason::INSUFFICIENT_SAMPLES,
                      csiClassifyDecision(false, true, true, true));
    TEST_ASSERT_EQUAL(CsiDecisionReason::INSUFFICIENT_SAMPLES,
                      csiClassifyDecision(false, false, false, false));
}

void test_clean_idle_is_variance_below() {
    // full buffer, no raw motion, not in motion, no hold → cleanly quiet
    TEST_ASSERT_EQUAL(CsiDecisionReason::VARIANCE_BELOW_THRESHOLD,
                      csiClassifyDecision(true, false, false, false));
}

void test_confirmed_motion_is_variance_above() {
    // raw motion + final motion → confirmed detection
    TEST_ASSERT_EQUAL(CsiDecisionReason::VARIANCE_ABOVE_THRESHOLD,
                      csiClassifyDecision(true, true, true, false));
}

void test_raw_motion_not_yet_confirmed_is_enter_pending() {
    // raw motion seen but smoothing has not accumulated enough votes → stays idle
    TEST_ASSERT_EQUAL(CsiDecisionReason::SMOOTHING_ENTER_PENDING,
                      csiClassifyDecision(true, true, false, false));
}

void test_holding_motion_without_raw_is_exit_pending() {
    // variance dropped (no raw motion) but smoothing keeps MOTION → exit pending
    TEST_ASSERT_EQUAL(CsiDecisionReason::SMOOTHING_EXIT_PENDING,
                      csiClassifyDecision(true, false, true, false));
}

void test_breathing_hold_dominates_final_state() {
    // breathing hold keeps MOTION even though detector went idle
    TEST_ASSERT_EQUAL(CsiDecisionReason::BREATHING_HOLD,
                      csiClassifyDecision(true, false, true, true));
    // hold flag wins over rawMotion classification too
    TEST_ASSERT_EQUAL(CsiDecisionReason::BREATHING_HOLD,
                      csiClassifyDecision(true, true, true, true));
}

// ---- reason -> string ------------------------------------------------------
void test_reason_strings_are_stable() {
    TEST_ASSERT_EQUAL_STRING("insufficient_samples",
        csiDecisionReasonStr(CsiDecisionReason::INSUFFICIENT_SAMPLES));
    TEST_ASSERT_EQUAL_STRING("variance_below_effective_threshold",
        csiDecisionReasonStr(CsiDecisionReason::VARIANCE_BELOW_THRESHOLD));
    TEST_ASSERT_EQUAL_STRING("variance_above_effective_threshold",
        csiDecisionReasonStr(CsiDecisionReason::VARIANCE_ABOVE_THRESHOLD));
    TEST_ASSERT_EQUAL_STRING("smoothing_enter_pending",
        csiDecisionReasonStr(CsiDecisionReason::SMOOTHING_ENTER_PENDING));
    TEST_ASSERT_EQUAL_STRING("smoothing_exit_pending",
        csiDecisionReasonStr(CsiDecisionReason::SMOOTHING_EXIT_PENDING));
    TEST_ASSERT_EQUAL_STRING("breathing_hold",
        csiDecisionReasonStr(CsiDecisionReason::BREATHING_HOLD));
}

// ---- struct defaults -------------------------------------------------------
void test_trace_defaults_invalid() {
    CsiDecisionTrace t;
    TEST_ASSERT_FALSE(t.valid);
    TEST_ASSERT_FALSE(t.decision);
    TEST_ASSERT_EQUAL(CsiDecisionReason::INSUFFICIENT_SAMPLES, t.reason);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_buffer_not_ready_is_insufficient_samples);
    RUN_TEST(test_clean_idle_is_variance_below);
    RUN_TEST(test_confirmed_motion_is_variance_above);
    RUN_TEST(test_raw_motion_not_yet_confirmed_is_enter_pending);
    RUN_TEST(test_holding_motion_without_raw_is_exit_pending);
    RUN_TEST(test_breathing_hold_dominates_final_state);
    RUN_TEST(test_reason_strings_are_stable);
    RUN_TEST(test_trace_defaults_invalid);
    return UNITY_END();
}
