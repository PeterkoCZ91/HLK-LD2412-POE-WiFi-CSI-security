// Native unit tests for the web low-heap accept gate (dev7).
// Pure — no Arduino. Run: pio test -e native -f test_heap_gate_policy
//
// Context: field coredump 2026-08-15 — async_tcp OOM abort inside the
// library's request-header parsing. The gate refuses NEW connections while
// the heap is under pressure so the parser never runs on a dead heap.
#include <unity.h>
#include "services/HeapGatePolicy.h"

void setUp() {}
void tearDown() {}

static HeapGateConfig defaults() {
    HeapGateConfig cfg;
    cfg.enabled           = true;
    cfg.closeFreeBytes    = 28 * 1024;
    cfg.openFreeBytes     = 40 * 1024;
    cfg.closeLargestBytes = 12 * 1024;
    cfg.openLargestBytes  = 16 * 1024;
    return cfg;
}

void test_accepts_when_heap_healthy() {
    HeapGatePolicy gate(defaults());
    TEST_ASSERT_TRUE(gate.shouldAccept(80 * 1024, 40 * 1024));
    TEST_ASSERT_FALSE(gate.isClosed());
    TEST_ASSERT_EQUAL_UINT32(0, gate.rejectsTotal());
    TEST_ASSERT_EQUAL_UINT32(0, gate.closeCount());
}

void test_closes_below_free_threshold() {
    HeapGatePolicy gate(defaults());
    TEST_ASSERT_FALSE(gate.shouldAccept(27 * 1024, 20 * 1024));
    TEST_ASSERT_TRUE(gate.isClosed());
    TEST_ASSERT_EQUAL_UINT32(1, gate.rejectsTotal());
    TEST_ASSERT_EQUAL_UINT32(1, gate.closeCount());
}

void test_closes_on_fragmentation_with_free_ok() {
    // Plenty of free heap in total, but the largest block is too small to
    // build a response — fragmentation must close the gate too.
    HeapGatePolicy gate(defaults());
    TEST_ASSERT_FALSE(gate.shouldAccept(50 * 1024, 11 * 1024));
    TEST_ASSERT_TRUE(gate.isClosed());
}

void test_stays_closed_in_hysteresis_band() {
    HeapGatePolicy gate(defaults());
    TEST_ASSERT_FALSE(gate.shouldAccept(27 * 1024, 20 * 1024));   // close
    // 30k free is above the close threshold but below the open threshold —
    // the gate must NOT flap open.
    TEST_ASSERT_FALSE(gate.shouldAccept(30 * 1024, 20 * 1024));
    TEST_ASSERT_TRUE(gate.isClosed());
    TEST_ASSERT_EQUAL_UINT32(2, gate.rejectsTotal());
    TEST_ASSERT_EQUAL_UINT32(1, gate.closeCount());               // one episode
}

void test_reopens_above_open_thresholds() {
    HeapGatePolicy gate(defaults());
    TEST_ASSERT_FALSE(gate.shouldAccept(27 * 1024, 20 * 1024));   // close
    TEST_ASSERT_TRUE(gate.shouldAccept(41 * 1024, 17 * 1024));    // reopen + accept
    TEST_ASSERT_FALSE(gate.isClosed());
}

void test_reopen_requires_both_free_and_largest() {
    HeapGatePolicy gate(defaults());
    TEST_ASSERT_FALSE(gate.shouldAccept(27 * 1024, 20 * 1024));   // close
    // free recovered but still fragmented → stay closed
    TEST_ASSERT_FALSE(gate.shouldAccept(41 * 1024, 15 * 1024));
    TEST_ASSERT_TRUE(gate.isClosed());
}

void test_close_count_counts_episodes() {
    HeapGatePolicy gate(defaults());
    TEST_ASSERT_FALSE(gate.shouldAccept(27 * 1024, 20 * 1024));   // episode 1
    TEST_ASSERT_TRUE(gate.shouldAccept(41 * 1024, 17 * 1024));    // reopen
    TEST_ASSERT_FALSE(gate.shouldAccept(10 * 1024, 20 * 1024));   // episode 2
    TEST_ASSERT_EQUAL_UINT32(2, gate.closeCount());
}

void test_probe_updates_state_without_counting_reject() {
    // The loop task probes once a second so the gate closes when the heap
    // collapses even if no connection arrives — but a probe is not a
    // connection and must not inflate rejectsTotal.
    HeapGatePolicy gate(defaults());
    gate.probe(27 * 1024, 20 * 1024);
    TEST_ASSERT_TRUE(gate.isClosed());
    TEST_ASSERT_EQUAL_UINT32(0, gate.rejectsTotal());
    TEST_ASSERT_EQUAL_UINT32(1, gate.closeCount());
    gate.probe(41 * 1024, 17 * 1024);
    TEST_ASSERT_FALSE(gate.isClosed());
    TEST_ASSERT_EQUAL_UINT32(0, gate.rejectsTotal());
}

void test_probe_disabled_never_closes() {
    HeapGateConfig cfg = defaults();
    cfg.enabled = false;
    HeapGatePolicy gate(cfg);
    gate.probe(1 * 1024, 1 * 1024);
    TEST_ASSERT_FALSE(gate.isClosed());
}

void test_disabled_always_accepts() {
    HeapGateConfig cfg = defaults();
    cfg.enabled = false;
    HeapGatePolicy gate(cfg);
    TEST_ASSERT_TRUE(gate.shouldAccept(1 * 1024, 1 * 1024));
    TEST_ASSERT_FALSE(gate.isClosed());
    TEST_ASSERT_EQUAL_UINT32(0, gate.rejectsTotal());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_accepts_when_heap_healthy);
    RUN_TEST(test_closes_below_free_threshold);
    RUN_TEST(test_closes_on_fragmentation_with_free_ok);
    RUN_TEST(test_stays_closed_in_hysteresis_band);
    RUN_TEST(test_reopens_above_open_thresholds);
    RUN_TEST(test_reopen_requires_both_free_and_largest);
    RUN_TEST(test_close_count_counts_episodes);
    RUN_TEST(test_probe_updates_state_without_counting_reject);
    RUN_TEST(test_probe_disabled_never_closes);
    RUN_TEST(test_disabled_always_accepts);
    return UNITY_END();
}
