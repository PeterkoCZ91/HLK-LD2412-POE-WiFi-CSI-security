#include <unity.h>
#include "services/TlsMemoryPolicy.h"

// dev12: the thresholds themselves moved (callers now pass MALLOC_CAP_8BIT
// figures instead of the ~42 kB inflated MALLOC_CAP_INTERNAL ones), so these
// tests assert the policy's contract against the constants rather than against
// hardcoded byte counts that have to be edited on every recalibration.

void setUp() {}
void tearDown() {}

static void test_rejects_low_free_heap() {
    TEST_ASSERT_FALSE(tlsMemoryAllowsHandshake(TLS_MIN_FREE_HEAP_BYTES - 1,
                                               TLS_MIN_LARGEST_BLOCK_BYTES + 10000));
    TEST_ASSERT_TRUE(tlsMemoryAllowsHandshake(TLS_MIN_FREE_HEAP_BYTES,
                                              TLS_MIN_LARGEST_BLOCK_BYTES));
}

static void test_rejects_fragmented_heap() {
    TEST_ASSERT_FALSE(tlsMemoryAllowsHandshake(TLS_MIN_FREE_HEAP_BYTES + 20000,
                                               TLS_MIN_LARGEST_BLOCK_BYTES - 1));
    TEST_ASSERT_TRUE(tlsMemoryAllowsHandshake(TLS_MIN_FREE_HEAP_BYTES + 20000,
                                              TLS_MIN_LARGEST_BLOCK_BYTES));
}

static void test_ca_allocation_is_budgeted() {
    const uint32_t ca = 2048;
    TEST_ASSERT_FALSE(tlsMemoryAllowsHandshake(TLS_MIN_FREE_HEAP_BYTES + ca - 1,
                                               TLS_MIN_LARGEST_BLOCK_BYTES + ca, ca));
    TEST_ASSERT_TRUE(tlsMemoryAllowsHandshake(TLS_MIN_FREE_HEAP_BYTES + ca,
                                              TLS_MIN_LARGEST_BLOCK_BYTES + ca, ca));
}

static void test_thresholds_stay_reachable_on_the_usable_heap() {
    // The byte-addressable heap on these boards tops out near 45 kB. A
    // threshold above that would not gate TLS, it would disable it — which is
    // exactly what the pre-dev12 48000/24000 pair did once the readings became
    // honest. Budget a typical 2 kB CA alongside.
    TEST_ASSERT_TRUE(TLS_MIN_FREE_HEAP_BYTES + 2048 < 45000);
    TEST_ASSERT_TRUE(TLS_MIN_LARGEST_BLOCK_BYTES + 2048 < TLS_MIN_FREE_HEAP_BYTES);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rejects_low_free_heap);
    RUN_TEST(test_rejects_fragmented_heap);
    RUN_TEST(test_ca_allocation_is_budgeted);
    RUN_TEST(test_thresholds_stay_reachable_on_the_usable_heap);
    return UNITY_END();
}
