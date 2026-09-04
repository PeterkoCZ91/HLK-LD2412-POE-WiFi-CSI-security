// Native unit tests for the heap low-water-mark tripwire.
// Pure — no Arduino/ESP. Run: pio test -e native -f test_heap_watermark_tripwire
//
// Context (field, 2026-08-31, dev16 12 h soak): min_free_8bit reached 7 700 B
// against a 48 200 B median — 6,6 kB BELOW the heap gate's close threshold and
// inside the band where this node has OOMed before. No 15 s sample ever caught
// it (the lowest was 29 504 B), so the spike is short and large, and four
// candidate causes were ruled out from the code (HA discovery publishes one
// entity per update() cycle, the offline buffer only stores alarm events on a
// disarmed node, WiFi scan is on-demand, csi queue_drops = 0).
//
// Sampling from outside cannot answer it: HTTP is slower than the event and
// allocates itself. The board has to notice its own watermark dropping and
// write down what was running at that moment. This policy decides WHEN that is
// worth a log line — the RTC log ring holds 20 entries total and is shared with
// ordinary logging, so a chatty tripwire would destroy the evidence it collects.
#include <unity.h>
#include "services/HeapWatermarkTripwire.h"

void setUp() {}
void tearDown() {}

// The first reading establishes the baseline. Recording it would burn a ring
// slot on "the heap is where it has always been".
void test_first_reading_is_baseline_only() {
    HeapWatermarkTripwire tw;
    TEST_ASSERT_FALSE(tw.evaluate(30000));
    TEST_ASSERT_EQUAL_UINT32(0, tw.recordCount());
}

// The watermark ratchets down in small steps all day; only a drop worth
// explaining earns a slot.
void test_drop_below_threshold_is_ignored() {
    HeapWatermarkTripwire tw(2048);
    tw.evaluate(30000);
    TEST_ASSERT_FALSE(tw.evaluate(29000));   // 1000 B — noise
    TEST_ASSERT_EQUAL_UINT32(0, tw.recordCount());
}

void test_drop_at_or_above_threshold_records() {
    HeapWatermarkTripwire tw(2048);
    tw.evaluate(30000);
    TEST_ASSERT_TRUE(tw.evaluate(27952));    // exactly 2048 B
    TEST_ASSERT_EQUAL_UINT32(1, tw.recordCount());
}

// The log line has to say "19868 -> 7700", so the value it dropped FROM must
// survive the call that reports the drop.
void test_reports_the_value_it_dropped_from() {
    HeapWatermarkTripwire tw(2048);
    tw.evaluate(19868);
    TEST_ASSERT_TRUE(tw.evaluate(7700));
    TEST_ASSERT_EQUAL_UINT32(19868, tw.previousWatermark());
    TEST_ASSERT_EQUAL_UINT32(7700, tw.lastRecorded());
}

// Ten 500 B steps are the same 5 kB of lost headroom as one 5 kB step. Comparing
// against the last RECORDED value (not the last seen one) makes them add up
// instead of each being dismissed as noise.
void test_small_drops_accumulate_until_they_matter() {
    HeapWatermarkTripwire tw(2048);
    tw.evaluate(30000);
    for (int i = 1; i <= 3; i++) TEST_ASSERT_FALSE(tw.evaluate(30000 - i * 500));
    TEST_ASSERT_TRUE(tw.evaluate(27900));    // 2100 B below the baseline
}

// After a record the baseline moves, so the same level does not re-fire.
void test_baseline_moves_after_recording() {
    HeapWatermarkTripwire tw(2048);
    tw.evaluate(30000);
    TEST_ASSERT_TRUE(tw.evaluate(20000));
    TEST_ASSERT_FALSE(tw.evaluate(20000));
    TEST_ASSERT_FALSE(tw.evaluate(19000));
}

// The ring holds 20 entries shared with ordinary logging. A tripwire that keeps
// writing would evict the very evidence it is collecting, so it stops.
void test_stops_after_the_record_cap() {
    HeapWatermarkTripwire tw(1000, 3);
    tw.evaluate(60000);
    TEST_ASSERT_TRUE(tw.evaluate(50000));
    TEST_ASSERT_TRUE(tw.evaluate(40000));
    TEST_ASSERT_TRUE(tw.evaluate(30000));
    TEST_ASSERT_FALSE(tw.evaluate(10000));   // cap reached
    TEST_ASSERT_EQUAL_UINT32(3, tw.recordCount());
}

// heap_caps_get_minimum_free_size() is monotonic within a boot, so a rise means
// our own state is stale, not that the heap recovered. Re-baseline silently
// rather than log a negative drop.
void test_a_rising_watermark_rebaselines_without_recording() {
    HeapWatermarkTripwire tw(2048);
    tw.evaluate(20000);
    TEST_ASSERT_FALSE(tw.evaluate(30000));
    TEST_ASSERT_EQUAL_UINT32(0, tw.recordCount());
    TEST_ASSERT_FALSE(tw.evaluate(29000));   // now measured against 30000
    TEST_ASSERT_TRUE(tw.evaluate(27000));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_reading_is_baseline_only);
    RUN_TEST(test_drop_below_threshold_is_ignored);
    RUN_TEST(test_drop_at_or_above_threshold_records);
    RUN_TEST(test_reports_the_value_it_dropped_from);
    RUN_TEST(test_small_drops_accumulate_until_they_matter);
    RUN_TEST(test_baseline_moves_after_recording);
    RUN_TEST(test_stops_after_the_record_cap);
    RUN_TEST(test_a_rising_watermark_rebaselines_without_recording);
    return UNITY_END();
}
