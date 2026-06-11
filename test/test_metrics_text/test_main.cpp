// Unit testy pro Prometheus text builder (IMPROVEMENTS T1) — pio test -e native
#include <unity.h>
#include <string.h>
#include "services/metrics_text.h"

void setUp() {}
void tearDown() {}

static MetricsSnapshot sample() {
    MetricsSnapshot m;
    m.fw_version = "v5.0.7";
    m.uptime_s = 4242;
    m.heap_free = 123456;
    m.heap_min = 100000;
    m.heap_largest = 80000;
    m.chip_temp_c = 47.5f;
    m.radar_connected = true;
    m.radar_frame_rate = 9.8f;
    m.radar_error_count = 3;
    m.radar_health_score = 95;
    m.eth_link_up = true;
    m.eth_speed_mbps = 100;
    m.mqtt_connected = true;
    m.mqtt_publish_fail_total = 7;
    m.mqtt_reconnect_total = 2;
    m.auth_ok_total = 50;
    m.auth_fail_total = 4;
    m.alarm_state = 4;  // TRIGGERED
    m.alarm_armed = true;
    m.fusion_enabled = true;
    m.fusion_presence = true;
    m.fusion_confidence = 0.875f;
    m.csi_present = true;
    m.csi_active = true;
    m.csi_packets_total = 999999;
    m.csi_packet_rate = 98.5f;
    m.csi_wifi_rssi_dbm = -71;
    m.csi_effective_threshold = 0.042f;
    m.csi_motion = false;
    m.csi_ml_probability = 0.123f;
    return m;
}

void test_contains_core_metrics() {
    char buf[4096];
    size_t len = buildMetricsText(sample(), buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_UINT((unsigned)strlen(buf), (unsigned)len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "poe2412_info{fw=\"v5.0.7\"} 1\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "poe2412_uptime_seconds 4242\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "poe2412_heap_free_bytes 123456\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "poe2412_chip_temp_celsius 47.5\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "poe2412_alarm_state 4\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "poe2412_http_auth_fail_total 4\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "poe2412_fusion_confidence 0.875\n"));
}

void test_csi_block_present() {
    char buf[4096];
    buildMetricsText(sample(), buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "poe2412_csi_packets_total 999999\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "poe2412_csi_wifi_rssi_dbm -71\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "poe2412_csi_motion 0\n"));
}

void test_csi_block_omitted_when_absent() {
    MetricsSnapshot m = sample();
    m.csi_present = false;
    char buf[4096];
    size_t len = buildMetricsText(m, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NULL(strstr(buf, "poe2412_csi_"));
}

void test_type_annotations() {
    char buf[4096];
    buildMetricsText(sample(), buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "# TYPE poe2412_uptime_seconds counter\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "# TYPE poe2412_heap_free_bytes gauge\n"));
}

// Příliš malý buffer → 0 (nikdy useknutý/nevalidní výstup)
void test_truncation_returns_zero() {
    char buf[64];
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)buildMetricsText(sample(), buf, sizeof(buf)));
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_contains_core_metrics);
    RUN_TEST(test_csi_block_present);
    RUN_TEST(test_csi_block_omitted_when_absent);
    RUN_TEST(test_type_annotations);
    RUN_TEST(test_truncation_returns_zero);
    return UNITY_END();
}
