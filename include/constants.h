#ifndef CONSTANTS_H
#define CONSTANTS_H

// =============================================================================
// Time Constants (milliseconds)
// =============================================================================

// Intervals
constexpr unsigned long INTERVAL_SSE_UPDATE_MS       = 250;       // SSE telemetry update
constexpr unsigned long INTERVAL_TELEMETRY_ACTIVE_MS = 2000;      // Telemetry when active
constexpr unsigned long INTERVAL_TELEMETRY_IDLE_MS   = 3600000;   // Telemetry when idle (1h)
constexpr unsigned long INTERVAL_CONNECTIVITY_MS     = 60000;     // Connectivity check (1 min)
constexpr unsigned long INTERVAL_UPTIME_SAVE_MS      = 3600000;   // Uptime save to NVS (1h — reduced NVS wear)
constexpr unsigned long INTERVAL_CERT_CHECK_MS       = 86400000;  // Certificate check (24h)
constexpr unsigned long INTERVAL_TELEMETRY_DIAG_MS   = 30000;     // Diagnostics tier (30s)
constexpr unsigned long INTERVAL_TELEMETRY_ENG_MS    = 10000;     // Engineering gates (10s, was 5s)

// Deadband thresholds (publish only when value changes by more than this)
constexpr uint16_t DEADBAND_DISTANCE_CM              = 5;
constexpr uint8_t  DEADBAND_ENERGY                   = 5;
constexpr int8_t   DEADBAND_RSSI                     = 3;
constexpr uint32_t DEADBAND_FREE_HEAP_KB             = 5;
constexpr uint8_t  DEADBAND_HEALTH_SCORE             = 3;
constexpr float    DEADBAND_FRAME_RATE               = 2.0f;
constexpr uint8_t  DEADBAND_GATE_ENERGY              = 3;

// Timeouts
constexpr unsigned long TIMEOUT_OTA_VALIDATION_MS    = 60000;     // OTA boot validation
constexpr unsigned long TIMEOUT_DMS_NO_PUBLISH_MS    = 1800000;   // Dead Man's Switch (30 min)
constexpr unsigned long TIMEOUT_DMS_STARTUP_MS       = 300000;    // DMS startup grace (5 min)
constexpr unsigned long TIMEOUT_MQTT_SOFT_RECOVERY_MS = 120000;   // Soft reconnect if publish stale (2 min, pre-DMS)
constexpr uint16_t MQTT_PUBLISH_FAIL_STREAK_LIMIT    = 3;         // Force disconnect after N consecutive publish() failures while connected
constexpr unsigned long INTERVAL_TIER3_HEARTBEAT_MS  = 300000;    // Force TIER 3 republish every 5 min (prevents HA staleness on unchanged values)
constexpr unsigned long TIMEOUT_GATE_VERIFY_MS       = 40000;     // Gate config verification delay (40s, post-revert window)

// Security Defaults
constexpr unsigned long DEFAULT_ENTRY_DELAY_MS       = 30000;     // Entry delay (30 sec)
constexpr unsigned long DEFAULT_EXIT_DELAY_MS        = 30000;     // Exit delay (30 sec)
constexpr unsigned long DEFAULT_ANTI_MASK_MS         = 300000;    // Anti-masking threshold (5 min)
constexpr unsigned long DEFAULT_LOITER_MS            = 15000;     // Loitering threshold (15 sec)
constexpr unsigned long DEFAULT_HEARTBEAT_MS         = 14400000;  // Heartbeat interval (4h)
constexpr unsigned long DEFAULT_TRIGGER_TIMEOUT_MS   = 900000;    // Alarm auto-silence after trigger (15 min)
constexpr uint8_t DEFAULT_ALARM_ENERGY_THRESHOLD     = 15;        // Min energy to trigger alarm (reduces false positives)

// =============================================================================
// Memory Thresholds
// =============================================================================
constexpr uint32_t HEAP_MIN_FOR_PUBLISH              = 20000;     // Min heap for MQTT publish
constexpr uint32_t HEAP_LOW_WARNING                  = 30000;     // Low memory warning threshold

// =============================================================================
// Hardware Configuration
// =============================================================================
constexpr uint8_t LED_PIN_DEFAULT                    = 25;
constexpr int8_t SIREN_PIN_DEFAULT                   = -1;        // Siren/strobe GPIO (-1 = disabled)
constexpr uint16_t DEFAULT_STARTUP_LED_SEC           = 120;       // LED blink duration after boot

// =============================================================================
// Radar Configuration
// =============================================================================
constexpr uint8_t RADAR_MIN_GATE_DEFAULT             = 0;
constexpr uint8_t RADAR_MAX_GATE_DEFAULT             = 13;
constexpr float RADAR_RESOLUTION_DEFAULT             = 0.75f;

// =============================================================================
// Network Configuration
// =============================================================================
constexpr uint8_t DMS_MAX_RESTARTS                   = 3;         // Dead Man's Switch max restarts

// =============================================================================
// Security Monitor Intervals
// =============================================================================
constexpr unsigned long INTERVAL_HEALTH_CHECK_MS     = 60000;     // Health check interval (1 min)
constexpr uint32_t HEAP_WARN_BYTES                   = 40000;     // Free heap warning threshold (40KB)
constexpr uint32_t HEAP_CRIT_BYTES                   = 20000;     // Free heap critical threshold (20KB)
constexpr uint32_t HEAP_RECOVER_BYTES                = 60000;     // Free heap recovery threshold (60KB)
constexpr unsigned long COOLDOWN_HEAP_ALERT_MS       = 600000;    // Heap alert cooldown (10 min)

constexpr float CHIP_TEMP_WARN_C                     = 80.0f;     // Chip temp warning threshold
constexpr float CHIP_TEMP_CRIT_C                     = 100.0f;    // Chip temp critical threshold
constexpr float CHIP_TEMP_RECOVER_C                  = 72.0f;     // Chip temp recovery (hysteresis -8°C)
constexpr unsigned long COOLDOWN_CHIP_TEMP_ALERT_MS  = 1800000;   // Chip temp alert cooldown (30 min)

constexpr unsigned long COOLDOWN_TAMPER_ALERT_MS     = 600000;    // Tamper alert cooldown (10 min)
constexpr unsigned long COOLDOWN_RADAR_ALERT_MS      = 300000;    // Radar alert cooldown (5 min)
constexpr unsigned long COOLDOWN_NETWORK_ANOMALY_MS  = 300000;    // ETH anomaly alert cooldown (5 min)
constexpr unsigned long TIMEOUT_RADAR_DISCONNECT_MS  = 30000;     // Radar disconnect threshold (30s)
constexpr unsigned long INTERVAL_RSSI_BASELINE_MS    = 30000;     // RSSI baseline establishment (30s)

// CSI WiFi RSSI thresholds (for /api/health warning surface)
constexpr int CSI_RSSI_WEAK_DBM                      = -70;       // Below this: low SNR, CSI degraded
constexpr int CSI_RSSI_HOT_DBM                       = -40;       // Above this: too close, may saturate

// =============================================================================
// Time Conversion
// =============================================================================
constexpr unsigned long MS_PER_MINUTE                = 60000;
constexpr unsigned long MS_PER_HOUR                  = 3600000;

#endif // CONSTANTS_H
