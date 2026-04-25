#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "secrets.h"

struct SystemConfig {
    char mqtt_server[60] = MQTT_SERVER_DEFAULT;
    char mqtt_port[6] = "1883";
    char mqtt_user[40] = MQTT_USER_DEFAULT;
    char mqtt_pass[40] = MQTT_PASS_DEFAULT;
    char mqtt_id[40] = "poe2412_device";
    char hostname[33] = "poe2412-node";
    char auth_user[20] = "admin";
    char auth_pass[20] = "admin";
    bool mqtt_enabled = true;
    bool led_enabled = true;
    uint16_t startup_led_sec = 120;
    float radar_resolution = 0.75;
    uint16_t chip_temp_interval = 600;  // MQTT publish interval (seconds), 0 = disabled
    // Static IP (empty = DHCP)
    char static_ip[16] = "";
    char static_gw[16] = "";
    char static_mask[16] = "255.255.255.0";
    char static_dns[16] = "";
    // Timezone offset (seconds from UTC, default GMT+1)
    int32_t tz_offset = 3600;
    int32_t dst_offset = 3600;
    // Scheduled arm/disarm (HH:MM format, empty = disabled)
    char sched_arm_time[6] = "";    // e.g. "22:00"
    char sched_disarm_time[6] = ""; // e.g. "07:00"
    // Auto-arm after N minutes of no presence (0 = disabled)
    uint16_t auto_arm_minutes = 0;

    // WiFi CSI (Channel State Information) — runtime config
    // Used only when firmware is compiled with -D USE_CSI=1
    bool     csi_enabled       = true;     // runtime enable (independent of compile flag)
    float    csi_threshold     = 0.5f;     // variance threshold for motion detection
    float    csi_hysteresis    = 0.7f;     // exit-multiplier (0.7 = stay in motion until var < 0.7*thr)
    uint16_t csi_window        = 75;       // sample window size (10–200)
    uint16_t csi_publish_ms    = 1000;     // MQTT publish interval (100–60000 ms)
    // Runtime-switchable SSID/pass for CSI sensor (overrides compile-time defaults).
    // Empty string => fall back to CSI_WIFI_SSID / CSI_WIFI_PASS from secrets.h at boot.
    char     csi_ssid[33]      = "";
    char     csi_pass[65]      = "";

    // Fusion — combined radar+CSI detection
    bool     fusion_enabled    = true;     // enable on-device radar+CSI fusion

    // Radar Bluetooth (HLK mobile-app pairing) — default OFF for security.
    // Applied on boot: if radar BT state differs from this flag, firmware sends
    // setBluetooth() + restartModule(). Existing deployed units are secured by
    // the first OTA update carrying v4.5.8+.
    bool     radar_bluetooth   = false;
};

class ConfigManager {
public:
    ConfigManager();
    void begin();
    void load();
    void save();

    SystemConfig& getConfig() { return _config; }
    
    bool isDefaultAuth() const {
        return (strcmp(_config.auth_user, "admin") == 0 && strcmp(_config.auth_pass, "admin") == 0);
    }

private:
    Preferences _prefs;
    SystemConfig _config;
    
    void loadPref(const char* key, char* target, size_t maxLen);
};

#endif
