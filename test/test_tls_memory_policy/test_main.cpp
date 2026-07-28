#include <unity.h>
#include "services/TlsMemoryPolicy.h"

void setUp() {}
void tearDown() {}

static void test_rejects_low_free_heap() {
    TEST_ASSERT_FALSE(tlsMemoryAllowsHandshake(47999, 40000));
    TEST_ASSERT_TRUE(tlsMemoryAllowsHandshake(48000, 24000));
}

static void test_rejects_fragmented_heap() {
    TEST_ASSERT_FALSE(tlsMemoryAllowsHandshake(80000, 23999));
    TEST_ASSERT_TRUE(tlsMemoryAllowsHandshake(80000, 24000));
}

static void test_ca_allocation_is_budgeted() {
    TEST_ASSERT_FALSE(tlsMemoryAllowsHandshake(49000, 26000, 2048));
    TEST_ASSERT_TRUE(tlsMemoryAllowsHandshake(50048, 26048, 2048));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rejects_low_free_heap);
    RUN_TEST(test_rejects_fragmented_heap);
    RUN_TEST(test_ca_allocation_is_budgeted);
    return UNITY_END();
}
