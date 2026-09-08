// Native unit tests for MlFeedbackRingState (IMPROVEMENTS T9) — the pure
// ring/pagination bookkeeping behind MlFeedbackStore's LittleFS-persisted
// labeled-sample ring. The disk I/O half (MlFeedbackStore itself) needs
// Arduino/LittleFS and is exercised on hardware; this covers the part that
// can regress silently and matters most — wraparound, sequence assignment
// and "everything after seq N" pagination not skipping or duplicating.
// Run: pio test -e native -f test_ml_feedback_store
#include <unity.h>
#include "services/MlFeedbackRingState.h"

void setUp() {}
void tearDown() {}

void test_fresh_ring_is_empty() {
    MlFeedbackRingState r(5);
    TEST_ASSERT_EQUAL_UINT32(0, r.count());
    TEST_ASSERT_EQUAL_UINT32(0, r.head());
    TEST_ASSERT_EQUAL_UINT32(0, r.lastSeq());
}

// First write gets seq 1 (matches CsiEventRing's convention), lands at
// physical slot 0, and subsequent writes (before the ring fills) are
// sequential in both seq and physical index.
void test_recordWrite_sequential_before_wrap() {
    MlFeedbackRingState r(5);
    uint32_t seq;
    TEST_ASSERT_EQUAL_UINT32(0, r.recordWrite(seq));
    TEST_ASSERT_EQUAL_UINT32(1, seq);
    TEST_ASSERT_EQUAL_UINT32(1, r.recordWrite(seq));
    TEST_ASSERT_EQUAL_UINT32(2, seq);
    TEST_ASSERT_EQUAL_UINT32(2, r.recordWrite(seq));
    TEST_ASSERT_EQUAL_UINT32(3, seq);
    TEST_ASSERT_EQUAL_UINT32(3, r.count());
    TEST_ASSERT_EQUAL_UINT32(3, r.lastSeq());
    TEST_ASSERT_EQUAL_UINT32(0, r.head());  // no wrap yet — oldest is still slot 0
}

// Capacity 3, write 5 — count caps at 3, the two oldest are evicted, head
// advances to the oldest surviving physical slot.
void test_wraparound_evicts_oldest_and_caps_count() {
    MlFeedbackRingState r(3);
    uint32_t seq;
    for (int i = 0; i < 5; i++) r.recordWrite(seq);
    TEST_ASSERT_EQUAL_UINT32(3, r.count());
    TEST_ASSERT_EQUAL_UINT32(5, r.lastSeq());
    // Writes 4 and 5 (0-indexed physical slots 0,1 got overwritten) — oldest
    // surviving physical slot is 2 (from write #3).
    TEST_ASSERT_EQUAL_UINT32(2, r.head());
}

// seqOf(i) must exactly match what recordWrite() actually assigned, in
// oldest-first order, even after wraparound — this is the property the
// disk export relies on to avoid re-reading seq from every sample.
void test_seqOf_matches_actual_assignment_after_wrap() {
    MlFeedbackRingState r(3);
    uint32_t seq;
    for (int i = 0; i < 5; i++) r.recordWrite(seq);  // seqs 1..5, capacity 3 -> surviving 3,4,5
    TEST_ASSERT_EQUAL_UINT32(3, r.seqOf(0));
    TEST_ASSERT_EQUAL_UINT32(4, r.seqOf(1));
    TEST_ASSERT_EQUAL_UINT32(5, r.seqOf(2));
}

// physIndexOf(i) must point at the physical slot that actually holds the
// i-th oldest surviving record (ring-wrapped from head).
void test_physIndexOf_wraps_from_head() {
    MlFeedbackRingState r(3);
    uint32_t seq;
    for (int i = 0; i < 5; i++) r.recordWrite(seq);  // head == 2
    TEST_ASSERT_EQUAL_UINT32(2, r.physIndexOf(0));
    TEST_ASSERT_EQUAL_UINT32(0, r.physIndexOf(1));  // wraps
    TEST_ASSERT_EQUAL_UINT32(1, r.physIndexOf(2));
}

// queryAfter must return exactly the records with seq > afterSeq, oldest
// first, no skips, no duplicates — the property the T9 plan explicitly
// asked to protect.
void test_queryAfter_no_skip_no_duplicate() {
    MlFeedbackRingState r(10);
    uint32_t seq;
    for (int i = 0; i < 5; i++) r.recordWrite(seq);  // seqs 1..5

    uint32_t idx[10];
    uint32_t n = r.queryAfter(/*afterSeq=*/2, /*limit=*/10, idx, 10);
    TEST_ASSERT_EQUAL_UINT32(3, n);  // seqs 3,4,5
    TEST_ASSERT_EQUAL_UINT32(3, r.seqOf(idx[0]));
    TEST_ASSERT_EQUAL_UINT32(4, r.seqOf(idx[1]));
    TEST_ASSERT_EQUAL_UINT32(5, r.seqOf(idx[2]));
}

// afterSeq == lastSeq (client already has everything) -> nothing returned.
void test_queryAfter_caught_up_returns_nothing() {
    MlFeedbackRingState r(10);
    uint32_t seq;
    for (int i = 0; i < 5; i++) r.recordWrite(seq);
    uint32_t idx[10];
    TEST_ASSERT_EQUAL_UINT32(0, r.queryAfter(5, 10, idx, 10));
}

// limit caps the page at the OLDEST unseen records, not an arbitrary subset —
// two successive pages (advancing afterSeq by the last seq returned) must
// cover every record exactly once with no gap.
void test_queryAfter_paginates_without_gaps() {
    MlFeedbackRingState r(20);
    uint32_t seq;
    for (int i = 0; i < 7; i++) r.recordWrite(seq);  // seqs 1..7

    uint32_t idx[10];
    uint32_t n1 = r.queryAfter(0, 3, idx, 10);
    TEST_ASSERT_EQUAL_UINT32(3, n1);
    TEST_ASSERT_EQUAL_UINT32(1, r.seqOf(idx[0]));
    TEST_ASSERT_EQUAL_UINT32(2, r.seqOf(idx[1]));
    TEST_ASSERT_EQUAL_UINT32(3, r.seqOf(idx[2]));

    uint32_t lastSeenSeq = r.seqOf(idx[2]);  // 3
    uint32_t n2 = r.queryAfter(lastSeenSeq, 3, idx, 10);
    TEST_ASSERT_EQUAL_UINT32(3, n2);
    TEST_ASSERT_EQUAL_UINT32(4, r.seqOf(idx[0]));
    TEST_ASSERT_EQUAL_UINT32(5, r.seqOf(idx[1]));
    TEST_ASSERT_EQUAL_UINT32(6, r.seqOf(idx[2]));

    lastSeenSeq = r.seqOf(idx[2]);  // 6
    uint32_t n3 = r.queryAfter(lastSeenSeq, 3, idx, 10);
    TEST_ASSERT_EQUAL_UINT32(1, n3);  // only seq 7 left
    TEST_ASSERT_EQUAL_UINT32(7, r.seqOf(idx[0]));
}

// outCap (caller's array size) bounds the result independently of `limit` —
// protects against a caller passing a limit larger than its own buffer.
void test_queryAfter_respects_outCap() {
    MlFeedbackRingState r(10);
    uint32_t seq;
    for (int i = 0; i < 5; i++) r.recordWrite(seq);
    uint32_t idx[2];
    TEST_ASSERT_EQUAL_UINT32(2, r.queryAfter(0, 10, idx, 2));
}

// restore() reproduces a disk-loaded ring's continuation behavior: a write
// after restore must get the next sequence number, not restart from 1.
void test_restore_continues_sequence_after_reboot() {
    MlFeedbackRingState before(5);
    uint32_t seq;
    for (int i = 0; i < 3; i++) before.recordWrite(seq);  // seqs 1..3, head=0, count=3

    MlFeedbackRingState after(5);  // simulates a fresh boot loading the header
    after.restore(before.head(), before.count(), before.nextSeq());
    TEST_ASSERT_EQUAL_UINT32(3, after.count());
    TEST_ASSERT_EQUAL_UINT32(3, after.lastSeq());

    uint32_t newSeq;
    after.recordWrite(newSeq);
    TEST_ASSERT_EQUAL_UINT32(4, newSeq);  // continues, doesn't restart at 1
}

// A persisted count larger than the current capacity (schema shrink) must
// clamp rather than overrun the caller's on-disk slot array.
void test_restore_clamps_count_to_capacity() {
    MlFeedbackRingState r(5);
    r.restore(0, 999, 1000);
    TEST_ASSERT_EQUAL_UINT32(5, r.count());
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_fresh_ring_is_empty);
    RUN_TEST(test_recordWrite_sequential_before_wrap);
    RUN_TEST(test_wraparound_evicts_oldest_and_caps_count);
    RUN_TEST(test_seqOf_matches_actual_assignment_after_wrap);
    RUN_TEST(test_physIndexOf_wraps_from_head);
    RUN_TEST(test_queryAfter_no_skip_no_duplicate);
    RUN_TEST(test_queryAfter_caught_up_returns_nothing);
    RUN_TEST(test_queryAfter_paginates_without_gaps);
    RUN_TEST(test_queryAfter_respects_outCap);
    RUN_TEST(test_restore_continues_sequence_after_reboot);
    RUN_TEST(test_restore_clamps_count_to_capacity);
    return UNITY_END();
}
