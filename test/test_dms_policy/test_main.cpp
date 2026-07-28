// Native unit tests for DMS counter-reset policy (pure, no Arduino).
// Root cause 2026-07-18: dms_count se nuloval hned po bootu, protože
// _lastPublish je inicializovaný na millis() — cap 3 restartů nikdy nenastal.
// Run: pio test -e native -f test_dms_policy
#include <unity.h>
#include "services/DmsPolicy.h"

void setUp() {}
void tearDown() {}

void test_no_reset_without_real_publish() {
    // Stav po bootu do mrtvého brokeru: čítač > 0, publish jen "inicializovaný"
    TEST_ASSERT_FALSE(dmsShouldResetCounter(2, /*publishedSinceBoot=*/false, /*stale=*/false));
}

void test_no_reset_when_stale() {
    TEST_ASSERT_FALSE(dmsShouldResetCounter(2, true, /*stale=*/true));
}

void test_reset_after_real_publish() {
    TEST_ASSERT_TRUE(dmsShouldResetCounter(2, true, false));
}

void test_no_reset_when_counter_zero() {
    TEST_ASSERT_FALSE(dmsShouldResetCounter(0, true, false));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_no_reset_without_real_publish);
    RUN_TEST(test_no_reset_when_stale);
    RUN_TEST(test_reset_after_real_publish);
    RUN_TEST(test_no_reset_when_counter_zero);
    return UNITY_END();
}
