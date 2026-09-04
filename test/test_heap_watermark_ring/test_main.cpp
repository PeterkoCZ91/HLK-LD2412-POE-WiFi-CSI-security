// Native unit tests for the tripwire's own RTC ring.
// Pure — no Arduino/ESP. Run: pio test -e native -f test_heap_watermark_ring
//
// Why this exists (field, 2026-08-31): the tripwire first wrote its findings
// through systemLog into LogRtcRing. That ring holds 20 entries TOTAL and is
// shared with ordinary logging — and on a node with a flapping Ethernet link the
// ETH handler turns the whole thing over in ~50 minutes. A check three hours
// after deployment found /api/logs holding 20 records, ALL of them "ETH link":
// the five tripwire lines that proved discovery drains 33 kB were already gone.
//
// A forensic instrument must not store its evidence where an unrelated, chattier
// producer can evict it. Hence a dedicated ring: fixed capacity matching the
// tripwire's own record cap, nobody else writes to it, and it lives in
// RTC-noinit memory so a panic reboot does not take the evidence with it.
#include <unity.h>
#include "services/HeapWatermarkRing.h"

void setUp() {}
void tearDown() {}

static HeapWatermarkRecord rec(uint32_t up, uint32_t prev, uint32_t wm) {
    HeapWatermarkRecord r{};
    r.uptimeS = up; r.prevWatermark = prev; r.watermark = wm;
    return r;
}

void test_init_produces_a_valid_empty_ring() {
    HeapWatermarkRtcRing ring;
    heapWmRingInit(ring);
    TEST_ASSERT_TRUE(heapWmRingValid(ring));
    TEST_ASSERT_EQUAL_UINT32(0, heapWmRingCount(ring));
}

// RTC memory after a cold power-on is whatever was in the cells. Without the
// magic+CRC check the firmware would publish that garbage as measurements.
void test_uninitialised_memory_is_rejected() {
    HeapWatermarkRtcRing ring;
    memset(&ring, 0xA5, sizeof(ring));
    TEST_ASSERT_FALSE(heapWmRingValid(ring));
}

void test_append_stores_the_record_and_counts_it() {
    HeapWatermarkRtcRing ring;
    heapWmRingInit(ring);
    heapWmRingAppend(ring, rec(13, 51740, 33576));
    TEST_ASSERT_EQUAL_UINT32(1, heapWmRingCount(ring));
    TEST_ASSERT_EQUAL_UINT32(13, ring.records[0].uptimeS);
    TEST_ASSERT_EQUAL_UINT32(51740, ring.records[0].prevWatermark);
    TEST_ASSERT_EQUAL_UINT32(33576, ring.records[0].watermark);
    TEST_ASSERT_TRUE(heapWmRingValid(ring));   // CRC kept in step
}

void test_records_are_kept_in_order() {
    HeapWatermarkRtcRing ring;
    heapWmRingInit(ring);
    for (uint32_t i = 0; i < 3; i++) heapWmRingAppend(ring, rec(i, 1000 + i, 900 + i));
    TEST_ASSERT_EQUAL_UINT32(3, heapWmRingCount(ring));
    for (uint32_t i = 0; i < 3; i++) TEST_ASSERT_EQUAL_UINT32(i, ring.records[i].uptimeS);
}

// The tripwire stops at its own record cap, so the ring should never overflow.
// If it somehow does, keep the EARLIEST records: the first drops after a boot
// are the ones that carry the discovery signature (§12.13), and a late chatty
// producer must not be able to push them out — that is the whole failure this
// ring exists to prevent.
void test_overflow_keeps_the_earliest_records() {
    HeapWatermarkRtcRing ring;
    heapWmRingInit(ring);
    for (uint32_t i = 0; i < HEAP_WM_RING_CAPACITY + 4; i++)
        heapWmRingAppend(ring, rec(i, 1000, 900));
    TEST_ASSERT_EQUAL_UINT32(HEAP_WM_RING_CAPACITY, heapWmRingCount(ring));
    TEST_ASSERT_EQUAL_UINT32(0, ring.records[0].uptimeS);
    TEST_ASSERT_EQUAL_UINT32(HEAP_WM_RING_CAPACITY - 1,
                             ring.records[HEAP_WM_RING_CAPACITY - 1].uptimeS);
}

// A record corrupted in place (bit rot, a stray write) must not be served as
// if it were measured data.
void test_corrupted_payload_fails_validation() {
    HeapWatermarkRtcRing ring;
    heapWmRingInit(ring);
    heapWmRingAppend(ring, rec(13, 51740, 33576));
    ring.records[0].watermark = 12345;      // tamper, CRC not recomputed
    TEST_ASSERT_FALSE(heapWmRingValid(ring));
}

// Survives a panic reboot: the firmware re-reads the same RTC cells and must
// accept them, because that is the only way the evidence outlives the crash.
void test_a_ring_carried_across_reboot_is_accepted() {
    HeapWatermarkRtcRing ring;
    heapWmRingInit(ring);
    heapWmRingAppend(ring, rec(4007, 27560, 5972));
    HeapWatermarkRtcRing carried;
    memcpy(&carried, &ring, sizeof(ring));   // what RTC-noinit does for us
    TEST_ASSERT_TRUE(heapWmRingValid(carried));
    TEST_ASSERT_EQUAL_UINT32(5972, carried.records[0].watermark);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_init_produces_a_valid_empty_ring);
    RUN_TEST(test_uninitialised_memory_is_rejected);
    RUN_TEST(test_append_stores_the_record_and_counts_it);
    RUN_TEST(test_records_are_kept_in_order);
    RUN_TEST(test_overflow_keeps_the_earliest_records);
    RUN_TEST(test_corrupted_payload_fails_validation);
    RUN_TEST(test_a_ring_carried_across_reboot_is_accepted);
    return UNITY_END();
}
