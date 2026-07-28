// Native unit tests for the RTC-noinit log ring (pure, no Arduino).
// Run: pio test -e native -f test_log_ring
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "services/LogRing.h"

void setUp() {}
void tearDown() {}

static LogEntry mk(const char* type, const char* msg, uint32_t ts) {
    LogEntry e; memset(&e, 0, sizeof(e));
    e.timestamp = ts;
    strncpy(e.type, type, sizeof(e.type) - 1);
    strncpy(e.message, msg, sizeof(e.message) - 1);
    return e;
}

void test_init_is_valid_and_empty() {
    LogRtcRing r; memset(&r, 0xAB, sizeof(r));  // simulace náhodného noinit obsahu
    TEST_ASSERT_FALSE(logRingValid(r));
    logRingInit(r);
    TEST_ASSERT_TRUE(logRingValid(r));
    TEST_ASSERT_EQUAL_UINT32(0, r.count);
}

void test_append_updates_crc_and_survives_copy() {
    LogRtcRing r; logRingInit(r);
    logRingAppend(r, mk("WARN", "hello", 42));
    TEST_ASSERT_TRUE(logRingValid(r));
    LogRtcRing copy = r;  // "reboot": obsah přežil beze změny
    TEST_ASSERT_TRUE(logRingValid(copy));
    LogEntry out[LOG_RING_CAPACITY];
    uint32_t n = logRingSnapshot(copy, out, LOG_RING_CAPACITY);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_STRING("hello", out[0].message);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].prev_boot);
}

void test_corruption_invalidates() {
    LogRtcRing r; logRingInit(r);
    logRingAppend(r, mk("INFO", "x", 1));
    r.entries[0].message[0] ^= 0xFF;  // bit flip bez přepočtu CRC
    TEST_ASSERT_FALSE(logRingValid(r));
}

void test_wraparound_keeps_newest_and_order() {
    LogRtcRing r; logRingInit(r);
    char msg[16];
    for (int i = 0; i < 25; i++) {  // 5 přes kapacitu 20
        snprintf(msg, sizeof(msg), "m%d", i);
        logRingAppend(r, mk("INFO", msg, (uint32_t)i));
    }
    TEST_ASSERT_EQUAL_UINT32(LOG_RING_CAPACITY, r.count);
    LogEntry out[LOG_RING_CAPACITY];
    uint32_t n = logRingSnapshot(r, out, LOG_RING_CAPACITY);
    TEST_ASSERT_EQUAL_UINT32(LOG_RING_CAPACITY, n);
    TEST_ASSERT_EQUAL_STRING("m5", out[0].message);    // oldest-first
    TEST_ASSERT_EQUAL_STRING("m24", out[n - 1].message);
}

void test_count_overflow_garbage_is_invalid() {
    LogRtcRing r; logRingInit(r);
    r.count = LOG_RING_CAPACITY + 5;  // i se správným CRC je to nesmysl
    r.crc = logRingCrc(r);
    TEST_ASSERT_FALSE(logRingValid(r));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_init_is_valid_and_empty);
    RUN_TEST(test_append_updates_crc_and_survives_copy);
    RUN_TEST(test_corruption_invalidates);
    RUN_TEST(test_wraparound_keeps_newest_and_order);
    RUN_TEST(test_count_overflow_garbage_is_invalid);
    return UNITY_END();
}
