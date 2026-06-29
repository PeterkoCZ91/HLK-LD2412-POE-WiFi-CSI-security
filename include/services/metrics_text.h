#ifndef METRICS_TEXT_H
#define METRICS_TEXT_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>

/**
 * Prometheus text exposition builder (IMPROVEMENTS T1).
 * Čistý header bez Arduino závislostí — unit testy v test/test_metrics_text.
 * Route /metrics v WebRoutes.cpp naplní MetricsSnapshot a zavolá
 * buildMetricsText(). Návrat 0 = buffer nestačil (výstup zahodit).
 */
struct MetricsSnapshot {
    const char* fw_version = "";
    uint32_t uptime_s = 0;
    uint32_t heap_free = 0;
    uint32_t heap_min = 0;
    uint32_t heap_largest = 0;
    float    chip_temp_c = 0;
    bool     radar_connected = false;
    bool     radar_monitoring_disabled = false;  // latched CSI-only until reboot
    float    radar_frame_rate = 0;
    uint32_t radar_error_count = 0;
    int      radar_health_score = 0;
    bool     eth_link_up = false;
    int      eth_speed_mbps = 0;
    bool     mqtt_connected = false;
    uint32_t mqtt_publish_fail_total = 0;
    uint32_t mqtt_reconnect_total = 0;
    uint32_t auth_ok_total = 0;
    uint32_t auth_fail_total = 0;
    int      alarm_state = 0;       // AlarmState: 0=DISARMED 1=ARMING 2=ARMED 3=PENDING 4=TRIGGERED
    bool     alarm_armed = false;
    bool     fusion_enabled = false;
    bool     fusion_presence = false;
    float    fusion_confidence = 0;
    bool     csi_present = false;   // false → csi_* metriky se vynechají (build bez USE_CSI)
    bool     csi_active = false;
    bool     csi_data_ok = true;    // false → WiFi associated but CSI frames starved
    uint32_t csi_packets_total = 0;
    float    csi_packet_rate = 0;
    int      csi_wifi_rssi_dbm = 0;
    float    csi_effective_threshold = 0;
    bool     csi_motion = false;
    float    csi_ml_probability = 0;
};

inline bool metricsAppendf(char* buf, size_t cap, size_t& pos, const char* fmt, ...) {
    if (pos >= cap) return false;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + pos, cap - pos, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - pos) return false;
    pos += (size_t)n;
    return true;
}

/** Vrací délku zapsaného textu (NUL-terminated), 0 při přetečení bufferu. */
inline size_t buildMetricsText(const MetricsSnapshot& m, char* buf, size_t cap) {
    size_t pos = 0;
    bool ok = true;
    #define METRIC(name, type, fmt, value) \
        ok = ok && metricsAppendf(buf, cap, pos, "# TYPE " name " " type "\n" name " " fmt "\n", value)

    ok = ok && metricsAppendf(buf, cap, pos,
        "# TYPE poe2412_info gauge\npoe2412_info{fw=\"%s\"} 1\n", m.fw_version);
    METRIC("poe2412_uptime_seconds",          "counter", "%lu",  (unsigned long)m.uptime_s);
    METRIC("poe2412_heap_free_bytes",         "gauge",   "%lu",  (unsigned long)m.heap_free);
    METRIC("poe2412_heap_min_free_bytes",     "gauge",   "%lu",  (unsigned long)m.heap_min);
    METRIC("poe2412_heap_largest_block_bytes","gauge",   "%lu",  (unsigned long)m.heap_largest);
    METRIC("poe2412_chip_temp_celsius",       "gauge",   "%.1f", (double)m.chip_temp_c);
    METRIC("poe2412_radar_connected",         "gauge",   "%d",   m.radar_connected ? 1 : 0);
    METRIC("poe2412_radar_monitoring_disabled","gauge",  "%d",   m.radar_monitoring_disabled ? 1 : 0);
    METRIC("poe2412_radar_frame_rate",        "gauge",   "%.1f", (double)m.radar_frame_rate);
    METRIC("poe2412_radar_errors_total",      "counter", "%lu",  (unsigned long)m.radar_error_count);
    METRIC("poe2412_radar_health_score",      "gauge",   "%d",   m.radar_health_score);
    METRIC("poe2412_eth_link_up",             "gauge",   "%d",   m.eth_link_up ? 1 : 0);
    METRIC("poe2412_eth_speed_mbps",          "gauge",   "%d",   m.eth_speed_mbps);
    METRIC("poe2412_mqtt_connected",          "gauge",   "%d",   m.mqtt_connected ? 1 : 0);
    METRIC("poe2412_mqtt_publish_fails_total","counter", "%lu",  (unsigned long)m.mqtt_publish_fail_total);
    METRIC("poe2412_mqtt_reconnects_total",   "counter", "%lu",  (unsigned long)m.mqtt_reconnect_total);
    METRIC("poe2412_http_auth_ok_total",      "counter", "%lu",  (unsigned long)m.auth_ok_total);
    METRIC("poe2412_http_auth_fail_total",    "counter", "%lu",  (unsigned long)m.auth_fail_total);
    ok = ok && metricsAppendf(buf, cap, pos,
        "# HELP poe2412_alarm_state 0=DISARMED 1=ARMING 2=ARMED 3=PENDING 4=TRIGGERED\n");
    METRIC("poe2412_alarm_state",             "gauge",   "%d",   m.alarm_state);
    METRIC("poe2412_alarm_armed",             "gauge",   "%d",   m.alarm_armed ? 1 : 0);
    METRIC("poe2412_fusion_enabled",          "gauge",   "%d",   m.fusion_enabled ? 1 : 0);
    METRIC("poe2412_fusion_presence",         "gauge",   "%d",   m.fusion_presence ? 1 : 0);
    METRIC("poe2412_fusion_confidence",       "gauge",   "%.3f", (double)m.fusion_confidence);
    if (m.csi_present) {
        METRIC("poe2412_csi_active",              "gauge",   "%d",   m.csi_active ? 1 : 0);
        METRIC("poe2412_csi_data_ok",             "gauge",   "%d",   m.csi_data_ok ? 1 : 0);
        METRIC("poe2412_csi_packets_total",       "counter", "%lu",  (unsigned long)m.csi_packets_total);
        METRIC("poe2412_csi_packet_rate",         "gauge",   "%.1f", (double)m.csi_packet_rate);
        METRIC("poe2412_csi_wifi_rssi_dbm",       "gauge",   "%d",   m.csi_wifi_rssi_dbm);
        METRIC("poe2412_csi_effective_threshold", "gauge",   "%.3f", (double)m.csi_effective_threshold);
        METRIC("poe2412_csi_motion",              "gauge",   "%d",   m.csi_motion ? 1 : 0);
        METRIC("poe2412_csi_ml_probability",      "gauge",   "%.3f", (double)m.csi_ml_probability);
    }
    #undef METRIC
    return ok ? pos : 0;
}

#endif // METRICS_TEXT_H
