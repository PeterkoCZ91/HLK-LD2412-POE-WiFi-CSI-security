// Native unit tests for the low-confidence corroboration gate (#3).
// Run: pio test -e native -f test_corroboration_window
#include <unity.h>
#include "services/CorroborationWindow.h"

void setUp() {}
void tearDown() {}

static CorrInputs mk(bool qualifies, float confidence, uint8_t source, uint32_t nowMs) {
    CorrInputs in;
    in.qualifies = qualifies;
    in.confidence = confidence;
    in.fusionSource = source;
    in.nowMs = nowMs;
    return in;
}

void test_high_confidence_passes_immediately() {
    CorroborationWindow window;
    TEST_ASSERT_TRUE(window.evaluate(mk(true, 0.9f, 0x1, 0)) == CorrGate::PASS);
}

void test_not_qualifying_passes_through_as_pass() {
    CorroborationWindow window;
    TEST_ASSERT_TRUE(window.evaluate(mk(false, 0.1f, 0x0, 0)) == CorrGate::PASS);
}

void test_low_confidence_holds_then_suppresses() {
    CorroborationWindow window;
    TEST_ASSERT_TRUE(window.evaluate(mk(true, 0.4f, 0x2, 0)) == CorrGate::HOLD);
    TEST_ASSERT_TRUE(window.evaluate(mk(true, 0.4f, 0x2, 5000)) == CorrGate::HOLD);
    TEST_ASSERT_TRUE(window.evaluate(mk(true, 0.4f, 0x2, 9000)) == CorrGate::SUPPRESS);
}

void test_second_modality_within_window_passes() {
    CorroborationWindow window;
    TEST_ASSERT_TRUE(window.evaluate(mk(true, 0.4f, 0x2, 0)) == CorrGate::HOLD);
    TEST_ASSERT_TRUE(window.evaluate(mk(true, 0.45f, 0x3, 3000)) == CorrGate::PASS);
}

void test_window_resets_after_suppress() {
    CorroborationWindow window;
    window.evaluate(mk(true, 0.4f, 0x2, 0));
    window.evaluate(mk(true, 0.4f, 0x2, 9000));
    TEST_ASSERT_TRUE(window.evaluate(mk(true, 0.4f, 0x2, 9500)) == CorrGate::HOLD);
}

void test_drop_to_not_qualifying_closes_window() {
    CorroborationWindow window;
    window.evaluate(mk(true, 0.4f, 0x2, 0));
    TEST_ASSERT_TRUE(window.evaluate(mk(false, 0.0f, 0x0, 1000)) == CorrGate::PASS);
    TEST_ASSERT_TRUE(window.evaluate(mk(true, 0.4f, 0x2, 2000)) == CorrGate::HOLD);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_high_confidence_passes_immediately);
    RUN_TEST(test_not_qualifying_passes_through_as_pass);
    RUN_TEST(test_low_confidence_holds_then_suppresses);
    RUN_TEST(test_second_modality_within_window_passes);
    RUN_TEST(test_window_resets_after_suppress);
    RUN_TEST(test_drop_to_not_qualifying_closes_window);
    return UNITY_END();
}
