// Native unit tests for the low-heap skip tracker.
// Pure — no Arduino/ESP. Run: pio test -e native -f test_heap_skip_tracker
//
// Context (bench .29, 2026-09-02, dev18 37 h soak): the serial log carries ten
// `[WARN] Low heap — skipping MQTT publish` lines. The STAB heartbeat five
// seconds either side of each one reports 59 180 B free, and the guard fires at
// 12 000 B — both call heapFreeUsable(), both inside the same loop() iteration
// with no return between them (main.cpp:2159 and main.cpp:2559). So the heap
// really does lose ~47 kB inside one pass and hand it straight back.
//
// The log line was useless for chasing it. It prints no number, so nothing says
// how deep the dip went, and it is rate-limited to one line per 10 s, so ten
// lines could be ten events or ten thousand — the field data cannot tell them
// apart. This policy fixes exactly that: count EVERY skip, log only sometimes,
// and keep the deepest reading even from the skips that were never logged.
#include <unity.h>
#include "services/HeapSkipTracker.h"

void setUp() {}
void tearDown() {}

void test_fresh_tracker_is_empty() {
    HeapSkipTracker t;
    TEST_ASSERT_EQUAL_UINT32(0, t.skips());
    TEST_ASSERT_EQUAL_UINT32(0, t.logged());
    TEST_ASSERT_EQUAL_UINT32(0, t.suppressed());
    TEST_ASSERT_FALSE(t.everSkipped());
}

// Nothing has been said yet, so the first event must always get a line.
void test_first_skip_logs() {
    HeapSkipTracker t(10000);
    TEST_ASSERT_TRUE(t.record(500, 9000));
    TEST_ASSERT_EQUAL_UINT32(1, t.skips());
    TEST_ASSERT_EQUAL_UINT32(1, t.logged());
    TEST_ASSERT_TRUE(t.everSkipped());
}

// The whole point: a suppressed line must still move the counter. This is the
// bug the field log had — ten lines, unknown number of events.
void test_suppressed_skip_is_still_counted() {
    HeapSkipTracker t(10000);
    t.record(500, 9000);
    TEST_ASSERT_FALSE(t.record(1200, 9000));
    TEST_ASSERT_FALSE(t.record(9000, 9000));
    TEST_ASSERT_EQUAL_UINT32(3, t.skips());
    TEST_ASSERT_EQUAL_UINT32(1, t.logged());
    TEST_ASSERT_EQUAL_UINT32(2, t.suppressed());
}

void test_logs_again_after_interval() {
    HeapSkipTracker t(10000);
    t.record(500, 9000);
    TEST_ASSERT_FALSE(t.record(10499, 9000));   // 9999 ms — still quiet
    TEST_ASSERT_TRUE(t.record(10500, 9000));    // exactly 10000 ms
    TEST_ASSERT_EQUAL_UINT32(2, t.logged());
}

// The deepest dip is the number worth having, and it is most likely to land in
// a burst — i.e. in exactly the events the rate limit throws away.
void test_lowest_free_comes_from_suppressed_events_too() {
    HeapSkipTracker t(10000);
    t.record(500, 9000);
    t.record(600, 1976);       // suppressed, but this is the interesting one
    t.record(700, 8000);
    TEST_ASSERT_EQUAL_UINT32(1976, t.lowestFree());
}

void test_lowest_free_never_rises() {
    HeapSkipTracker t(0);
    t.record(500, 4000);
    t.record(600, 30000);
    TEST_ASSERT_EQUAL_UINT32(4000, t.lowestFree());
}

// Reported only once there is something to report — otherwise a dashboard shows
// a fake "0 B free" on a node that has never skipped.
void test_lowest_free_is_sentinel_until_first_skip() {
    HeapSkipTracker t;
    TEST_ASSERT_EQUAL_UINT32(HEAP_SKIP_NO_READING, t.lowestFree());
}

// millis() wraps every 49,7 days and these nodes run longer than that. A naive
// signed comparison would go quiet for the rest of the boot. Unsigned
// subtraction has to carry the elapsed time across the wrap correctly, so the
// boundary is checked from both sides.
void test_survives_millis_wraparound() {
    HeapSkipTracker t(10000);
    TEST_ASSERT_TRUE(t.record(0xFFFFF000u, 9000));
    // 0x00000FF0 is 8176 ms later once the wrap is accounted for — still inside
    // the quiet window, so this must NOT produce a line.
    TEST_ASSERT_FALSE(t.record(0x00000FF0u, 9000));
    // 0x00002000 is 12288 ms later — past the window.
    TEST_ASSERT_TRUE(t.record(0x00002000u, 9000));
    TEST_ASSERT_EQUAL_UINT32(3, t.skips());
    TEST_ASSERT_EQUAL_UINT32(2, t.logged());
}

void test_zero_interval_logs_every_skip() {
    HeapSkipTracker t(0);
    TEST_ASSERT_TRUE(t.record(100, 5000));
    TEST_ASSERT_TRUE(t.record(100, 5000));
    TEST_ASSERT_EQUAL_UINT32(2, t.logged());
    TEST_ASSERT_EQUAL_UINT32(0, t.suppressed());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fresh_tracker_is_empty);
    RUN_TEST(test_first_skip_logs);
    RUN_TEST(test_suppressed_skip_is_still_counted);
    RUN_TEST(test_logs_again_after_interval);
    RUN_TEST(test_lowest_free_comes_from_suppressed_events_too);
    RUN_TEST(test_lowest_free_never_rises);
    RUN_TEST(test_lowest_free_is_sentinel_until_first_skip);
    RUN_TEST(test_survives_millis_wraparound);
    RUN_TEST(test_zero_interval_logs_every_skip);
    return UNITY_END();
}
