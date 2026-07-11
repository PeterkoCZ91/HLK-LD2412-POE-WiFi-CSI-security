// Native unit tests for the CSI diagnostic event ring buffer (P1.3, navrh 17.2).
// Pure — no Arduino. Run: pio test -e native -f test_csi_events
#include <unity.h>
#include "services/CsiEventRing.h"

void setUp() {}
void tearDown() {}

static CsiEvent mk(CsiEventType t, float var) {
    CsiEvent e; e.type = t; e.variance = var; return e;
}

void test_push_assigns_monotonic_seq() {
    CsiEventRing<8> r;
    TEST_ASSERT_EQUAL_UINT32(1, r.push(mk(CsiEventType::MOTION_ENTER, 0.1f)));
    TEST_ASSERT_EQUAL_UINT32(2, r.push(mk(CsiEventType::MOTION_EXIT, 0.0f)));
    TEST_ASSERT_EQUAL_UINT16(2, r.count());
    TEST_ASSERT_EQUAL_UINT32(2, r.lastSeq());
}

void test_last_seq_zero_before_first_push() {
    CsiEventRing<8> r;
    TEST_ASSERT_EQUAL_UINT32(0, r.lastSeq());
    TEST_ASSERT_EQUAL_UINT16(0, r.count());
}

void test_wraparound_keeps_capacity_and_newest() {
    CsiEventRing<4> r;
    for (int i = 0; i < 6; i++) r.push(mk(CsiEventType::VARIANCE_SPIKE, (float)i));
    TEST_ASSERT_EQUAL_UINT16(4, r.count());     // capped at capacity
    TEST_ASSERT_EQUAL_UINT32(6, r.lastSeq());   // seq keeps climbing

    CsiEvent out[8];
    uint16_t n = r.query(0, 8, out, 8);
    TEST_ASSERT_EQUAL_UINT16(4, n);
    // oldest-first: the two oldest (seq 1,2) were overwritten → seq 3..6 remain
    TEST_ASSERT_EQUAL_UINT32(3, out[0].seq);
    TEST_ASSERT_EQUAL_UINT32(6, out[3].seq);
}

void test_query_after_seq_filters() {
    CsiEventRing<8> r;
    for (int i = 0; i < 5; i++) r.push(mk(CsiEventType::MOTION_ENTER, 0.0f));  // seq 1..5
    CsiEvent out[8];
    uint16_t n = r.query(3, 8, out, 8);   // only seq > 3
    TEST_ASSERT_EQUAL_UINT16(2, n);
    TEST_ASSERT_EQUAL_UINT32(4, out[0].seq);
    TEST_ASSERT_EQUAL_UINT32(5, out[1].seq);
}

void test_query_respects_limit() {
    CsiEventRing<8> r;
    for (int i = 0; i < 6; i++) r.push(mk(CsiEventType::MOTION_ENTER, 0.0f));
    CsiEvent out[8];
    uint16_t n = r.query(0, 3, out, 8);   // limit 3
    TEST_ASSERT_EQUAL_UINT16(3, n);
    TEST_ASSERT_EQUAL_UINT32(1, out[0].seq);
    TEST_ASSERT_EQUAL_UINT32(3, out[2].seq);
}

void test_query_respects_out_cap() {
    CsiEventRing<8> r;
    for (int i = 0; i < 6; i++) r.push(mk(CsiEventType::MOTION_ENTER, 0.0f));
    CsiEvent out[2];
    uint16_t n = r.query(0, 100, out, 2);  // outCap 2 dominates
    TEST_ASSERT_EQUAL_UINT16(2, n);
}

void test_clear_keeps_seq_monotonic() {
    CsiEventRing<8> r;
    r.push(mk(CsiEventType::MOTION_ENTER, 0.0f));
    r.push(mk(CsiEventType::MOTION_EXIT, 0.0f));
    r.clear();
    TEST_ASSERT_EQUAL_UINT16(0, r.count());
    // next seq continues from 3, not reset to 1
    TEST_ASSERT_EQUAL_UINT32(3, r.push(mk(CsiEventType::MOTION_ENTER, 0.0f)));
}

void test_query_after_latest_returns_none() {
    CsiEventRing<8> r;
    for (int i = 0; i < 4; i++) r.push(mk(CsiEventType::MOTION_ENTER, 0.0f));
    CsiEvent out[8];
    TEST_ASSERT_EQUAL_UINT16(0, r.query(r.lastSeq(), 8, out, 8));
}

void test_type_strings() {
    TEST_ASSERT_EQUAL_STRING("motion_enter", csiEventTypeStr(CsiEventType::MOTION_ENTER));
    TEST_ASSERT_EQUAL_STRING("variance_spike", csiEventTypeStr(CsiEventType::VARIANCE_SPIKE));
    TEST_ASSERT_EQUAL_STRING("model_disagreement", csiEventTypeStr(CsiEventType::MODEL_DISAGREEMENT));
    TEST_ASSERT_EQUAL_STRING("health_change", csiEventTypeStr(CsiEventType::HEALTH_CHANGE));
}

void test_health_flags_roundtrip() {
    // HEALTH_CHANGE events carry the flag snapshot through the ring intact.
    CsiEventRing<8> r;
    CsiEvent e; e.type = CsiEventType::HEALTH_CHANGE; e.healthFlags = 0x0142;
    r.push(e);
    CsiEvent out[2];
    uint16_t n = r.query(0, 2, out, 2);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    TEST_ASSERT_EQUAL(CsiEventType::HEALTH_CHANGE, out[0].type);
    TEST_ASSERT_EQUAL_UINT16(0x0142, out[0].healthFlags);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_push_assigns_monotonic_seq);
    RUN_TEST(test_last_seq_zero_before_first_push);
    RUN_TEST(test_wraparound_keeps_capacity_and_newest);
    RUN_TEST(test_query_after_seq_filters);
    RUN_TEST(test_query_respects_limit);
    RUN_TEST(test_query_respects_out_cap);
    RUN_TEST(test_clear_keeps_seq_monotonic);
    RUN_TEST(test_query_after_latest_returns_none);
    RUN_TEST(test_type_strings);
    RUN_TEST(test_health_flags_roundtrip);
    return UNITY_END();
}
