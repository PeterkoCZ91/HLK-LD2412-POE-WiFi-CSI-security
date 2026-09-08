// Native unit tests for heap metric capability selection (dev12).
// Pure — no Arduino. Run: pio test -e native -f test_heap_metrics
//
// Field root cause 2026-08-20, a production node: nine oom_gate restarts in
// 6.3 h while /api/health reported 78-87 kB free and the dev7 web heap gate
// never closed once (close_count 0, rejects_total 0). Every heap decision read
// ESP.getFreeHeap()/getMaxAllocHeap(), and arduino-esp32 implements both over
// MALLOC_CAP_INTERNAL — which counts the IRAM-only heap region that malloc and
// operator new can never allocate from. These tests pin the distinction so a
// memory decision can never silently go back to the inflated number.
#include <unity.h>
#include "services/HeapMetrics.h"
#include "services/HeapGatePolicy.h"

void setUp() {}
void tearDown() {}

// The nine field markers, verbatim: "oom_gate heap=<freeInternal>/<largestInternal>".
// largestInternal was byte-identical (40948) in all nine — that is the whole
// leftover IRAM region (0x40096000..0x400A0000 = 5 x 0x2000 = 40960 B, minus
// 12 B multi_heap overhead), so it was entirely free every time while the
// byte-addressable heap was empty.
static HeapReading fieldOomReading() {
    HeapReading r;
    r.freeInternal    = 45892;   // marker #1
    r.largestInternal = 40948;
    r.freeUsable      = 3952;    // 45892 - 41940 unusable IRAM
    r.largestUsable   = 2048;
    r.minFreeUsable   = 1400;
    return r;
}

// ---- unusable-byte accounting -----------------------------------------------
void test_unusable_internal_bytes_is_the_iram_region() {
    // 40960 B page run + 980 B tail after _iram_end, less multi_heap overhead
    TEST_ASSERT_EQUAL_UINT32(41940, heapUnusableInternalBytes(fieldOomReading()));
}

void test_unusable_bytes_never_underflows() {
    // usable can momentarily exceed internal across two unlocked reads
    HeapReading r;
    r.freeInternal = 1000;
    r.freeUsable   = 1200;
    TEST_ASSERT_EQUAL_UINT32(0, heapUnusableInternalBytes(r));
}

void test_internal_is_flagged_misleading_in_the_field_case() {
    TEST_ASSERT_TRUE(heapInternalIsMisleading(fieldOomReading()));
}

void test_internal_is_not_misleading_when_iram_is_consumed() {
    // a board whose IRAM leftover is small: internal ~= usable, thresholds
    // expressed either way would agree
    HeapReading r;
    r.freeInternal    = 42000;
    r.largestInternal = 20000;
    r.freeUsable      = 41000;
    r.largestUsable   = 20000;
    TEST_ASSERT_FALSE(heapInternalIsMisleading(r));
}

// ---- why the field gate was inert -------------------------------------------
void test_shipped_gate_stays_open_on_internal_numbers() {
    // Characterisation of the bug: fed the numbers the field actually fed it,
    // the gate cannot close — 45892 > closeFree and 40948 > closeLargest. This
    // is the whole reason the node restarted instead of shedding web load.
    HeapGatePolicy gate;
    HeapReading r = fieldOomReading();
    gate.probe(r.freeInternal, r.largestInternal);
    TEST_ASSERT_FALSE(gate.isClosed());
    TEST_ASSERT_EQUAL_UINT32(0, gate.closeCount());
}

void test_shipped_gate_closes_on_usable_numbers() {
    // Same instant, honest numbers: the gate must shut the accept path.
    HeapGatePolicy gate;
    HeapReading r = fieldOomReading();
    gate.probe(r.freeUsable, r.largestUsable);
    TEST_ASSERT_TRUE(gate.isClosed());
    TEST_ASSERT_EQUAL_UINT32(1, gate.closeCount());
    TEST_ASSERT_FALSE(gate.shouldAccept(r.freeUsable, r.largestUsable));
    TEST_ASSERT_EQUAL_UINT32(1, gate.rejectsTotal());
}

// ---- recalibrated defaults vs. the node's real working band ------------------
void test_defaults_do_not_close_in_the_normal_band() {
    // The field node runs ~36-45 kB real byte-addressable free heap with ~15-20 kB
    // largest block; the gate must stay out of the way there.
    HeapGatePolicy gate;
    gate.probe(40000, 15000);
    TEST_ASSERT_FALSE(gate.isClosed());
    TEST_ASSERT_TRUE(gate.shouldAccept(36000, 12000));
}

void test_defaults_close_before_the_observed_oom_range() {
    // OOM struck between 1.4 and 13 kB usable free. Closing must begin above
    // that whole range, not inside it.
    HeapGatePolicy gate;
    gate.probe(13000, 8000);
    TEST_ASSERT_TRUE(gate.isClosed());
}

void test_default_thresholds_are_expressed_in_usable_bytes() {
    // Guards the recalibration: the dev7 values (28 kB / 40 kB) were reverse
    // engineered from inflated internal readings and sit above this node's
    // entire usable-heap band, so they must not come back.
    // Bound raised from 20 to 32 KiB for the 5.7.1 recalibration (bench dev6,
    // 2026-09-06): real steady-state free heap once MQTT is connected is
    // ~32 kB, ~14 kB below the older ~45 kB assumption this file used to
    // encode (see docs/RELEASE_5.7.1_VALIDATION.md) — closeFreeBytes moved to
    // 20 KiB to keep real margin at that baseline, still well inside a
    // plausible byte-addressable band, unlike the inflated dev7 values.
    HeapGateConfig cfg;
    TEST_ASSERT_TRUE(cfg.closeFreeBytes < 32u * 1024u);
    TEST_ASSERT_TRUE(cfg.openFreeBytes > cfg.closeFreeBytes);
    TEST_ASSERT_TRUE(cfg.closeLargestBytes < 8u * 1024u);
    TEST_ASSERT_TRUE(cfg.openLargestBytes > cfg.closeLargestBytes);
}

void test_hysteresis_still_needs_both_metrics_to_recover() {
    HeapGatePolicy gate;
    gate.probe(1000, 500);
    TEST_ASSERT_TRUE(gate.isClosed());
    gate.probe(30000, 5000);          // free recovered, largest block did not
    TEST_ASSERT_TRUE(gate.isClosed());
    gate.probe(30000, 12000);
    TEST_ASSERT_FALSE(gate.isClosed());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_unusable_internal_bytes_is_the_iram_region);
    RUN_TEST(test_unusable_bytes_never_underflows);
    RUN_TEST(test_internal_is_flagged_misleading_in_the_field_case);
    RUN_TEST(test_internal_is_not_misleading_when_iram_is_consumed);
    RUN_TEST(test_shipped_gate_stays_open_on_internal_numbers);
    RUN_TEST(test_shipped_gate_closes_on_usable_numbers);
    RUN_TEST(test_defaults_do_not_close_in_the_normal_band);
    RUN_TEST(test_defaults_close_before_the_observed_oom_range);
    RUN_TEST(test_default_thresholds_are_expressed_in_usable_bytes);
    RUN_TEST(test_hysteresis_still_needs_both_metrics_to_recover);
    return UNITY_END();
}
