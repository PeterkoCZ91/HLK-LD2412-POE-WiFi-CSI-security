// Unit testy pro AuthLockout (IMPROVEMENTS T3) — pio test -e native
#include <unity.h>
#include "services/AuthLockout.h"

static const uint32_t IP1 = 0xC0A80001;  // 192.168.0.1
static const uint32_t IP2 = 0xC0A80002;

void setUp() {}
void tearDown() {}

// 4 neúspěchy pod prahem (5) → žádný lockout
void test_allows_below_threshold() {
    AuthLockout l;
    uint32_t t = 1000;
    for (int i = 0; i < 4; i++) l.onFailure(IP1, t += 100);
    TEST_ASSERT_EQUAL_UINT32(0, l.lockedForMs(IP1, t + 1));
}

// 5 neúspěchů v okně → lock na BASE_LOCK_MS
void test_locks_after_threshold() {
    AuthLockout l;
    uint32_t t = 1000;
    for (int i = 0; i < 5; i++) l.onFailure(IP1, t += 100);
    TEST_ASSERT_EQUAL_UINT32(AuthLockout::BASE_LOCK_MS, l.lockedForMs(IP1, t));
}

// Lock po uplynutí BASE_LOCK_MS vyprší
void test_lock_expires() {
    AuthLockout l;
    uint32_t t = 1000;
    for (int i = 0; i < 5; i++) l.onFailure(IP1, t += 100);
    TEST_ASSERT_EQUAL_UINT32(0, l.lockedForMs(IP1, t + AuthLockout::BASE_LOCK_MS + 1));
}

// Druhý lockout je dvojnásobný (exponenciální backoff)
void test_exponential_backoff() {
    AuthLockout l;
    uint32_t t = 1000;
    for (int i = 0; i < 5; i++) l.onFailure(IP1, t += 100);        // 1. lock (30 s)
    t += AuthLockout::BASE_LOCK_MS + 1000;                          // počkat na vypršení
    for (int i = 0; i < 5; i++) l.onFailure(IP1, t += 100);        // 2. lock
    TEST_ASSERT_EQUAL_UINT32(2 * AuthLockout::BASE_LOCK_MS, l.lockedForMs(IP1, t));
}

// Backoff je zastropovaný na MAX_LOCK_MS
void test_backoff_cap() {
    AuthLockout l;
    uint32_t t = 1000;
    for (int round = 0; round < 8; round++) {
        for (int i = 0; i < 5; i++) l.onFailure(IP1, t += 100);
        t += AuthLockout::MAX_LOCK_MS + 1000;  // přečkat i nejdelší možný lock
    }
    for (int i = 0; i < 5; i++) l.onFailure(IP1, t += 100);
    TEST_ASSERT_EQUAL_UINT32(AuthLockout::MAX_LOCK_MS, l.lockedForMs(IP1, t));
}

// Neúspěchy rozprostřené > WINDOW_MS od sebe nikdy nezamknou
void test_window_resets() {
    AuthLockout l;
    uint32_t t = 0;
    for (int i = 0; i < 20; i++) {
        t += AuthLockout::WINDOW_MS + 1000;
        l.onFailure(IP1, t);
    }
    TEST_ASSERT_EQUAL_UINT32(0, l.lockedForMs(IP1, t));
}

// Úspěšné přihlášení resetuje čítač
void test_success_clears() {
    AuthLockout l;
    uint32_t t = 1000;
    for (int i = 0; i < 4; i++) l.onFailure(IP1, t += 100);
    l.onSuccess(IP1);
    for (int i = 0; i < 4; i++) l.onFailure(IP1, t += 100);
    TEST_ASSERT_EQUAL_UINT32(0, l.lockedForMs(IP1, t + 1));
}

// Lockout je per-IP — IP2 není ovlivněna lockem IP1
void test_ips_independent() {
    AuthLockout l;
    uint32_t t = 1000;
    for (int i = 0; i < 5; i++) l.onFailure(IP1, t += 100);
    TEST_ASSERT_TRUE(l.lockedForMs(IP1, t) > 0);
    TEST_ASSERT_EQUAL_UINT32(0, l.lockedForMs(IP2, t));
}

// millis() wraparound: lock začínající těsně před přetečením přežije přetečení
void test_millis_wraparound() {
    AuthLockout l;
    uint32_t t = 0xFFFFFF00u;  // ~256 ms před přetečením
    for (int i = 0; i < 5; i++) l.onFailure(IP1, t += 10);  // t = 0xFFFFFF50
    TEST_ASSERT_TRUE(l.lockedForMs(IP1, 5000) > 0);  // už po přetečení, stále locked
    TEST_ASSERT_EQUAL_UINT32(0, l.lockedForMs(IP1, (uint32_t)(t + AuthLockout::BASE_LOCK_MS + 1)));
}

// Tabulka má MAX_ENTRIES slotů; nejstarší záznam je vytlačen (dokumentovaný tradeoff)
void test_eviction_oldest() {
    AuthLockout l;
    uint32_t t = 1000;
    for (int i = 0; i < 5; i++) l.onFailure(IP1, t += 10);
    TEST_ASSERT_TRUE(l.lockedForMs(IP1, t) > 0);
    for (uint32_t k = 1; k <= AuthLockout::MAX_ENTRIES; k++) {
        l.onFailure(0x0A000000u + k, t + 100 * k);
    }
    TEST_ASSERT_EQUAL_UINT32(0, l.lockedForMs(IP1, t + 1000));
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_allows_below_threshold);
    RUN_TEST(test_locks_after_threshold);
    RUN_TEST(test_lock_expires);
    RUN_TEST(test_exponential_backoff);
    RUN_TEST(test_backoff_cap);
    RUN_TEST(test_window_resets);
    RUN_TEST(test_success_clears);
    RUN_TEST(test_ips_independent);
    RUN_TEST(test_millis_wraparound);
    RUN_TEST(test_eviction_oldest);
    return UNITY_END();
}
