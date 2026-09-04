// Native unit tests for the CSI passage-edge JSON payload (pure, no Arduino).
// Run: pio test -e native -f test_csi_motion_edge
#include <unity.h>
#include <string.h>
#include "services/CsiMotionEdge.h"

void setUp() {}
void tearDown() {}

static bool has(const char* hay, const char* needle) { return strstr(hay, needle) != nullptr; }

void test_enter_event_is_motion_started() {
    char buf[256];
    int w = formatCsiMotionEdge(buf, sizeof(buf), "a3f19c04", 7, true,
                                918230, "csi_variance", 0.004212f, 0.003152f);
    TEST_ASSERT_GREATER_THAN(0, w);
    TEST_ASSERT_TRUE(has(buf, "\"event\":\"motion_started\""));
    TEST_ASSERT_TRUE(has(buf, "\"boot_id\":\"a3f19c04\""));
    TEST_ASSERT_TRUE(has(buf, "\"seq\":7"));
    TEST_ASSERT_TRUE(has(buf, "\"uptime_ms\":918230"));
    TEST_ASSERT_TRUE(has(buf, "\"source\":\"csi_variance\""));
    TEST_ASSERT_TRUE(has(buf, "\"v\":1"));
}

void test_exit_event_is_motion_ended() {
    char buf[256];
    int w = formatCsiMotionEdge(buf, sizeof(buf), "a3f19c04", 8, false,
                                919000, "csi_variance", 0.000382f, 0.003152f);
    TEST_ASSERT_GREATER_THAN(0, w);
    TEST_ASSERT_TRUE(has(buf, "\"event\":\"motion_ended\""));
    TEST_ASSERT_TRUE(has(buf, "\"seq\":8"));
}

void test_returned_length_matches_strlen() {
    char buf[256];
    int w = formatCsiMotionEdge(buf, sizeof(buf), "deadbeef", 1, true,
                                1000, "csi_variance", 0.1f, 0.2f);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), w);
}

void test_truncation_returns_zero() {
    char buf[16];  // far too small
    int w = formatCsiMotionEdge(buf, sizeof(buf), "deadbeef", 1, true,
                                1000, "csi_variance", 0.1f, 0.2f);
    TEST_ASSERT_EQUAL_INT(0, w);  // signalled, never a half-written payload
}

void test_null_args_return_zero() {
    char buf[64];
    TEST_ASSERT_EQUAL_INT(0, formatCsiMotionEdge(nullptr, 64, "x", 1, true, 1, "s", 0, 0));
    TEST_ASSERT_EQUAL_INT(0, formatCsiMotionEdge(buf, 0, "x", 1, true, 1, "s", 0, 0));
    TEST_ASSERT_EQUAL_INT(0, formatCsiMotionEdge(buf, 64, nullptr, 1, true, 1, "s", 0, 0));
    TEST_ASSERT_EQUAL_INT(0, formatCsiMotionEdge(buf, 64, "x", 1, true, 1, nullptr, 0, 0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_enter_event_is_motion_started);
    RUN_TEST(test_exit_event_is_motion_ended);
    RUN_TEST(test_returned_length_matches_strlen);
    RUN_TEST(test_truncation_returns_zero);
    RUN_TEST(test_null_args_return_zero);
    return UNITY_END();
}
