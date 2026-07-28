#include <cstdio>
#include <cstring>
#include <unity.h>
#include "services/WifiScanResults.h"

void setUp() {}
void tearDown() {}

void test_ignores_hidden_networks() {
    WifiScanResults r;
    TEST_ASSERT_FALSE(r.add("", -40, 1, 3, false));
    TEST_ASSERT_EQUAL_UINT8(0, r.count());
}

void test_deduplicates_ssid_and_keeps_strongest_ap() {
    WifiScanResults r;
    r.add("lab", -72, 1, 3, true);
    r.add("lab", -45, 11, 3, true);
    r.add("lab", -60, 6, 3, true);

    TEST_ASSERT_EQUAL_UINT8(1, r.count());
    TEST_ASSERT_EQUAL_STRING("lab", r.at(0).ssid);
    TEST_ASSERT_EQUAL_INT16(-45, r.at(0).rssi);
    TEST_ASSERT_EQUAL_UINT8(11, r.at(0).channel);
    TEST_ASSERT_TRUE(r.at(0).current);
}

void test_sorts_by_signal_descending() {
    WifiScanResults r;
    r.add("weak", -90, 1, 3, false);
    r.add("strong", -35, 6, 3, false);
    r.add("middle", -60, 11, 0, false);

    TEST_ASSERT_EQUAL_STRING("strong", r.at(0).ssid);
    TEST_ASSERT_EQUAL_STRING("middle", r.at(1).ssid);
    TEST_ASSERT_EQUAL_STRING("weak", r.at(2).ssid);
}

void test_capacity_keeps_twenty_strongest_unique_ssids() {
    WifiScanResults r;
    char ssid[12];
    for (int i = 0; i < 25; i++) {
        std::snprintf(ssid, sizeof(ssid), "ap-%02d", i);
        r.add(ssid, -100 + i, 1 + (i % 13), 3, false);
    }

    TEST_ASSERT_EQUAL_UINT8(WifiScanResults::CAPACITY, r.count());
    TEST_ASSERT_EQUAL_INT16(-76, r.at(0).rssi);
    TEST_ASSERT_EQUAL_INT16(-95, r.at(19).rssi);
}

void test_capacity_remains_bounded_under_repeated_replacement() {
    WifiScanResults r;
    char ssid[12];
    for (int i = 0; i < 1000; i++) {
        std::snprintf(ssid, sizeof(ssid), "ap-%03d", i);
        r.add(ssid, -127 + (i % 128), 1 + (i % 13), 3, false);
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(WifiScanResults::CAPACITY, r.count());
        for (uint8_t j = 1; j < r.count(); j++) {
            TEST_ASSERT_GREATER_OR_EQUAL_INT16(r.at(j).rssi, r.at(j - 1).rssi);
        }
    }
}

void test_ssid_is_bounded_to_32_bytes() {
    WifiScanResults r;
    r.add("abcdefghijklmnopqrstuvwxyz123456789", -50, 1, 3, false);
    TEST_ASSERT_EQUAL_UINT32(32, std::strlen(r.at(0).ssid));
    TEST_ASSERT_EQUAL('\0', r.at(0).ssid[32]);
}

void test_state_and_failure_labels_are_stable_for_api_and_mqtt() {
    TEST_ASSERT_EQUAL_STRING("idle", wifiScanStateText(WifiScanState::IDLE));
    TEST_ASSERT_EQUAL_STRING("running", wifiScanStateText(WifiScanState::RUNNING));
    TEST_ASSERT_EQUAL_STRING("complete", wifiScanStateText(WifiScanState::READY));
    TEST_ASSERT_EQUAL_STRING("failed", wifiScanStateText(WifiScanState::FAILED));
    TEST_ASSERT_EQUAL_STRING("", wifiScanFailureText(WifiScanFailureReason::NONE));
    TEST_ASSERT_EQUAL_STRING("csi_pause_failed",
                             wifiScanFailureText(WifiScanFailureReason::CSI_PAUSE_FAILED));
    TEST_ASSERT_EQUAL_STRING("driver_start_failed",
                             wifiScanFailureText(WifiScanFailureReason::DRIVER_START_FAILED));
    TEST_ASSERT_EQUAL_STRING("driver_failed",
                             wifiScanFailureText(WifiScanFailureReason::DRIVER_FAILED));
    TEST_ASSERT_EQUAL_STRING("timeout",
                             wifiScanFailureText(WifiScanFailureReason::TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("csi_restore_failed",
                             wifiScanFailureText(WifiScanFailureReason::CSI_RESTORE_FAILED));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ignores_hidden_networks);
    RUN_TEST(test_deduplicates_ssid_and_keeps_strongest_ap);
    RUN_TEST(test_sorts_by_signal_descending);
    RUN_TEST(test_capacity_keeps_twenty_strongest_unique_ssids);
    RUN_TEST(test_capacity_remains_bounded_under_repeated_replacement);
    RUN_TEST(test_ssid_is_bounded_to_32_bytes);
    RUN_TEST(test_state_and_failure_labels_are_stable_for_api_and_mqtt);
    return UNITY_END();
}
