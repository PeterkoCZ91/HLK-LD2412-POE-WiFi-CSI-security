// Native unit tests for the OOM last-resort guard (dev7).
// Pure — no Arduino. Run: pio test -e native -f test_oom_marker
//
// std::set_new_handler fires when a throwing `new` cannot be satisfied. The
// handler must not allocate: it stamps an RTC-noinit marker and restarts.
// The marker survives the software reset (not power loss) and is folded into
// reset_history as "oom_gate" on the next boot.
#include <unity.h>
#include <string.h>
#include "services/OomGuard.h"

void setUp() {}
void tearDown() {}

void test_set_marker_is_valid_and_carries_fields() {
    OomMarker m;
    oomMarkerSet(m, 15918, 1234, 512);
    TEST_ASSERT_TRUE(oomMarkerValid(m));
    TEST_ASSERT_EQUAL_UINT32(15918, m.uptimeS);
    TEST_ASSERT_EQUAL_UINT32(1234, m.freeBytes);
    TEST_ASSERT_EQUAL_UINT32(512, m.largestBytes);
}

void test_cleared_marker_is_invalid() {
    OomMarker m;
    oomMarkerSet(m, 100, 0, 0);
    oomMarkerClear(m);
    TEST_ASSERT_FALSE(oomMarkerValid(m));
}

void test_corrupted_field_invalidates_marker() {
    OomMarker m;
    oomMarkerSet(m, 100, 2048, 1024);
    m.uptimeS ^= 0x1;   // single-bit flip must break the checksum
    TEST_ASSERT_FALSE(oomMarkerValid(m));
}

void test_rtc_garbage_is_invalid() {
    // RTC noinit RAM after power-up is arbitrary — pattern-fill must not
    // masquerade as a valid marker.
    OomMarker m;
    memset(&m, 0xA5, sizeof(m));
    TEST_ASSERT_FALSE(oomMarkerValid(m));
    memset(&m, 0x00, sizeof(m));
    TEST_ASSERT_FALSE(oomMarkerValid(m));
}

void test_restart_when_enabled_and_unobstructed() {
    TEST_ASSERT_TRUE(oomShouldRestart(true, false, false, 15918));
}

void test_abort_when_restart_disabled() {
    // oom_coredump debug flag: operator wants the panic + dump back.
    TEST_ASSERT_FALSE(oomShouldRestart(false, false, false, 15918));
}

void test_abort_when_reboot_inhibited() {
    // OTA flash write in progress — restarting mid-write risks a brick;
    // the abort path at least leaves a coredump.
    TEST_ASSERT_FALSE(oomShouldRestart(true, true, false, 15918));
}

void test_abort_breaks_oom_restart_loop() {
    // Previous boot already ended in an oom_gate restart and we are OOM
    // again within the guard window — restarting would loop invisibly.
    TEST_ASSERT_FALSE(oomShouldRestart(true, false, true, 30));
}

void test_restart_after_loop_guard_window() {
    // Previous oom_gate restart, but this boot survived past the window —
    // treat it as a fresh incident.
    TEST_ASSERT_TRUE(oomShouldRestart(true, false, true, 90));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_set_marker_is_valid_and_carries_fields);
    RUN_TEST(test_cleared_marker_is_invalid);
    RUN_TEST(test_corrupted_field_invalidates_marker);
    RUN_TEST(test_rtc_garbage_is_invalid);
    RUN_TEST(test_restart_when_enabled_and_unobstructed);
    RUN_TEST(test_abort_when_restart_disabled);
    RUN_TEST(test_abort_when_reboot_inhibited);
    RUN_TEST(test_abort_breaks_oom_restart_loop);
    RUN_TEST(test_restart_after_loop_guard_window);
    return UNITY_END();
}
