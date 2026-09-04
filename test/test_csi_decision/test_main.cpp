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

// ---- data starvation (dev7: field false trigger 2026-08-17) ------------
void test_data_starved_dominates_when_buffer_ready() {
    // pps=0 froze the variance above the threshold for ~8 s — whatever the
    // frozen values say, a starved tick must classify as DATA_STARVED.
    TEST_ASSERT_EQUAL(CsiDecisionReason::DATA_STARVED,
                      csiClassifyDecision(true, true, true, false, true));
    TEST_ASSERT_EQUAL(CsiDecisionReason::DATA_STARVED,
                      csiClassifyDecision(true, false, false, false, true));
    // starved wins over breathing hold too — stale data is no hold evidence
    TEST_ASSERT_EQUAL(CsiDecisionReason::DATA_STARVED,
                      csiClassifyDecision(true, false, true, true, true));
}

void test_empty_buffer_dominates_starved() {
    // no buffer at all is the more fundamental "no decision" reason
    TEST_ASSERT_EQUAL(CsiDecisionReason::INSUFFICIENT_SAMPLES,
                      csiClassifyDecision(false, false, false, false, true));
}

void test_default_not_starved_keeps_legacy_classification() {
    // 4-arg callers (pre-dev7) must classify exactly as before
    TEST_ASSERT_EQUAL(CsiDecisionReason::VARIANCE_ABOVE_THRESHOLD,
                      csiClassifyDecision(true, true, true, false));
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
    TEST_ASSERT_EQUAL_STRING("data_starved",
        csiDecisionReasonStr(CsiDecisionReason::DATA_STARVED));
}

// ---- struct defaults -------------------------------------------------------
void test_trace_defaults_invalid() {
    CsiDecisionTrace t;
    TEST_ASSERT_FALSE(t.valid);
    TEST_ASSERT_FALSE(t.decision);
    TEST_ASSERT_EQUAL(CsiDecisionReason::INSUFFICIENT_SAMPLES, t.reason);
    TEST_ASSERT_FALSE(t.dataStarved);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, t.packetRate);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_buffer_not_ready_is_insufficient_samples);
    RUN_TEST(test_clean_idle_is_variance_below);
    RUN_TEST(test_confirmed_motion_is_variance_above);
    RUN_TEST(test_raw_motion_not_yet_confirmed_is_enter_pending);
    RUN_TEST(test_holding_motion_without_raw_is_exit_pending);
    RUN_TEST(test_breathing_hold_dominates_final_state);
    RUN_TEST(test_data_starved_dominates_when_buffer_ready);
    RUN_TEST(test_empty_buffer_dominates_starved);
    RUN_TEST(test_default_not_starved_keeps_legacy_classification);
    RUN_TEST(test_reason_strings_are_stable);
    RUN_TEST(test_trace_defaults_invalid);
    return UNITY_END();
}
