// Native unit tests for the boot outage Telegram notice (dev8).
// Pure — no Arduino. Run: pio test -e native -f test_boot_outage
//
// Context: 10 panics over 13.-15.8. surfaced only because someone read
// reset_history by hand. A dirty boot (panic/WDT/brownout/oom_gate) now
// pushes a Telegram notice with everything the marker/NVS remembered.
#include <unity.h>
#include <string.h>
#include "services/BootOutageNotice.h"

void setUp() {}
void tearDown() {}

// ---- dirty-boot decision ----------------------------------------------------
void test_panic_is_dirty() {
    TEST_ASSERT_TRUE(bootOutageIsDirty("Exception/Panic", "none"));
}

void test_watchdogs_and_brownout_are_dirty() {
    TEST_ASSERT_TRUE(bootOutageIsDirty("Interrupt WDT", "none"));
    TEST_ASSERT_TRUE(bootOutageIsDirty("Task WDT", "none"));
    TEST_ASSERT_TRUE(bootOutageIsDirty("Other WDT", "none"));
    TEST_ASSERT_TRUE(bootOutageIsDirty("Brownout", "none"));
}

void test_oom_gate_cause_is_dirty_even_on_sw_reset() {
    // the oom_gate restart is a controlled esp_restart() — reason reads as a
    // plain software reset, only the cause string carries the incident
    TEST_ASSERT_TRUE(bootOutageIsDirty("Software reset", "oom_gate heap=1234/512"));
}

void test_clean_boots_are_quiet() {
    // OTA / user restart / DMS = intentional; power-on = smart-plug cycle is
    // the normal reboot path on PoE sites, must not spam
    TEST_ASSERT_FALSE(bootOutageIsDirty("Software reset", "none"));
    TEST_ASSERT_FALSE(bootOutageIsDirty("Software reset", "user_restart"));
    TEST_ASSERT_FALSE(bootOutageIsDirty("Power-on", "none"));
    TEST_ASSERT_FALSE(bootOutageIsDirty("Deep sleep", "none"));
    TEST_ASSERT_FALSE(bootOutageIsDirty("Unknown", "none"));
}

// ---- message format ----------------------------------------------------------
static BootOutageInfo sample() {
    BootOutageInfo i;
    i.resetReason = "Exception/Panic";
    i.restartCause = "none";
    i.fwVersion = "v5.7.0-test";
    i.uptimeS = 15918;          // 4 h 25 m
    i.prevHeap = 1234;
    i.prevMaxAlloc = 512;
    i.prevMinHeap = 900;
    i.coredumpPresent = true;
    return i;
}

void test_format_contains_reason_uptime_heap_coredump() {
    char buf[400];
    int n = formatBootOutageNotice(buf, sizeof(buf), sample());
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "firmware: v5.7.0-test"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Exception/Panic"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "4h 25m"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "1234/512/900"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "coredump: yes"));
}

void test_format_skips_unknown_heap_and_cause() {
    BootOutageInfo i = sample();
    i.prevHeap = 0;             // 0 = never recorded (hard crash, no safeRestart)
    i.coredumpPresent = false;
    char buf[400];
    formatBootOutageNotice(buf, sizeof(buf), i);
    TEST_ASSERT_NULL(strstr(buf, "heap before"));
    TEST_ASSERT_NULL(strstr(buf, "cause:"));      // "none" adds nothing
    TEST_ASSERT_NOT_NULL(strstr(buf, "coredump: no"));
}

void test_format_includes_nontrivial_cause() {
    BootOutageInfo i = sample();
    i.resetReason = "Software reset";
    i.restartCause = "oom_gate heap=1234/512";
    char buf[400];
    formatBootOutageNotice(buf, sizeof(buf), i);
    TEST_ASSERT_NOT_NULL(strstr(buf, "oom_gate heap=1234/512"));
}

void test_format_uptime_days() {
    BootOutageInfo i = sample();
    i.uptimeS = 2 * 86400 + 3 * 3600 + 60;   // 2d 3h 1m
    char buf[400];
    formatBootOutageNotice(buf, sizeof(buf), i);
    TEST_ASSERT_NOT_NULL(strstr(buf, "2d 3h 1m"));
}

void test_format_respects_capacity() {
    char buf[24];
    int n = formatBootOutageNotice(buf, sizeof(buf), sample());
    TEST_ASSERT_LESS_THAN(24, n < 0 ? 0 : (int)strlen(buf));
    TEST_ASSERT_EQUAL_CHAR('\0', buf[strlen(buf)]);
}

// ---- MQTT event JSON (HA forwards these to Telegram — non-retained) ---------
void test_event_json_contains_all_fields() {
    char buf[400];
    int n = formatBootOutageEventJson(buf, sizeof(buf), sample());
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"v\":1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"boot_outage\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"fw_version\":\"v5.7.0-test\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"reason\":\"Exception/Panic\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"uptime_s\":15918"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"heap_free\":1234"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"heap_maxalloc\":512"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"heap_min\":900"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"coredump\":true"));
}

void test_event_json_cause_empty_when_none() {
    // "none" is the NVS default, not information — HA templates get ""
    char buf[400];
    formatBootOutageEventJson(buf, sizeof(buf), sample());
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cause\":\"\""));
    BootOutageInfo i = sample();
    i.restartCause = "oom_gate heap=1234/512";
    formatBootOutageEventJson(buf, sizeof(buf), i);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cause\":\"oom_gate heap=1234/512\""));
}

void test_gate_episode_json() {
    char buf[200];
    int n = formatHeapGateEventJson(buf, sizeof(buf), 42, 17, 45000, 28000);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"event\":\"heap_gate_episode\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"closed_s\":42"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"rejects_total\":17"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"heap_free\":45000"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"heap_min\":28000"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_panic_is_dirty);
    RUN_TEST(test_watchdogs_and_brownout_are_dirty);
    RUN_TEST(test_oom_gate_cause_is_dirty_even_on_sw_reset);
    RUN_TEST(test_clean_boots_are_quiet);
    RUN_TEST(test_format_contains_reason_uptime_heap_coredump);
    RUN_TEST(test_format_skips_unknown_heap_and_cause);
    RUN_TEST(test_format_includes_nontrivial_cause);
    RUN_TEST(test_format_uptime_days);
    RUN_TEST(test_format_respects_capacity);
    RUN_TEST(test_event_json_contains_all_fields);
    RUN_TEST(test_event_json_cause_empty_when_none);
    RUN_TEST(test_gate_episode_json);
    return UNITY_END();
}
