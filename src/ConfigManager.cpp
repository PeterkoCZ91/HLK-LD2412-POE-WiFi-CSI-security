#include "ConfigManager.h"
#include "debug.h"

ConfigManager::ConfigManager() {}

void ConfigManager::begin() {
    _prefs.begin("radar_config", false);
    load();
}

void ConfigManager::load() {
    loadPref("mqtt_server", _config.mqtt_server, sizeof(_config.mqtt_server));
    loadPref("mqtt_port", _config.mqtt_port, sizeof(_config.mqtt_port));
    loadPref("mqtt_user", _config.mqtt_user, sizeof(_config.mqtt_user));
    loadPref("mqtt_pass", _config.mqtt_pass, sizeof(_config.mqtt_pass));
    loadPref("mqtt_id", _config.mqtt_id, sizeof(_config.mqtt_id));
    loadPref("hostname", _config.hostname, sizeof(_config.hostname));
    loadPref("auth_user", _config.auth_user, sizeof(_config.auth_user));
    loadPref("auth_pass", _config.auth_pass, sizeof(_config.auth_pass));

    _config.mqtt_enabled = _prefs.getBool("mqtt_en", true);
    _config.led_enabled = _prefs.getBool("led_en", true);
    _config.startup_led_sec = _prefs.getUInt("led_start", 120);

    if (_prefs.isKey("radar_res")) {
        _config.radar_resolution = _prefs.getFloat("radar_res", 0.75);
    }
    _config.chip_temp_interval = _prefs.getUShort("temp_intv", 600);

    // Static IP
    loadPref("static_ip", _config.static_ip, sizeof(_config.static_ip));
    loadPref("static_gw", _config.static_gw, sizeof(_config.static_gw));
    loadPref("static_mask", _config.static_mask, sizeof(_config.static_mask));
    loadPref("static_dns", _config.static_dns, sizeof(_config.static_dns));

    // Timezone
    _config.tz_offset = _prefs.getInt("tz_offset", 3600);
    _config.dst_offset = _prefs.getInt("dst_offset", 3600);

    // Scheduled arm/disarm
    loadPref("sched_arm", _config.sched_arm_time, sizeof(_config.sched_arm_time));
    loadPref("sched_disarm", _config.sched_disarm_time, sizeof(_config.sched_disarm_time));

    // Auto-arm
    _config.auto_arm_minutes = _prefs.getUShort("auto_arm_min", 0);

    // WiFi CSI runtime config
    _config.csi_enabled    = _prefs.getBool ("csi_en",     true);
    _config.csi_threshold  = _prefs.getFloat("csi_thr",    0.5f);
    _config.csi_hysteresis = _prefs.getFloat("csi_hyst",   0.7f);
    _config.csi_window     = _prefs.getUShort("csi_win",   75);
    _config.csi_publish_ms = _prefs.getUShort("csi_pubms", 1000);
    _config.fusion_enabled = _prefs.getBool ("fus_en",     true);
    loadPref("csi_ssid", _config.csi_ssid, sizeof(_config.csi_ssid));
    loadPref("csi_pass", _config.csi_pass, sizeof(_config.csi_pass));

    // Radar Bluetooth (HLK app pairing) — default OFF
    _config.radar_bluetooth = _prefs.getBool("radar_bt", false);

    // Persist defaults to NVS on first boot (prevents NOT_FOUND errors)
    if (!_prefs.isKey("mqtt_server")) {
        save();
        DBG("CONFIG", "First boot — defaults written to NVS");
    }

    DBG("CONFIG", "Configuration loaded from NVS");
    if (isDefaultAuth()) {
        Serial.println("[SECURITY] WARNING: Default credentials (admin/admin) are in use!");
    }
}

void ConfigManager::save() {
    _prefs.putString("mqtt_server", _config.mqtt_server);
    _prefs.putString("mqtt_port", _config.mqtt_port);
    _prefs.putString("mqtt_user", _config.mqtt_user);
    _prefs.putString("mqtt_pass", _config.mqtt_pass);
    _prefs.putString("mqtt_id", _config.mqtt_id);
    _prefs.putString("hostname", _config.hostname);
    _prefs.putString("auth_user", _config.auth_user);
    _prefs.putString("auth_pass", _config.auth_pass);

    _prefs.putBool("mqtt_en", _config.mqtt_enabled);
    _prefs.putBool("led_en", _config.led_enabled);
    _prefs.putUInt("led_start", _config.startup_led_sec);
    _prefs.putFloat("radar_res", _config.radar_resolution);
    _prefs.putUShort("temp_intv", _config.chip_temp_interval);

    // Static IP
    _prefs.putString("static_ip", _config.static_ip);
    _prefs.putString("static_gw", _config.static_gw);
    _prefs.putString("static_mask", _config.static_mask);
    _prefs.putString("static_dns", _config.static_dns);

    // Timezone
    _prefs.putInt("tz_offset", _config.tz_offset);
    _prefs.putInt("dst_offset", _config.dst_offset);

    // Scheduled arm/disarm
    _prefs.putString("sched_arm", _config.sched_arm_time);
    _prefs.putString("sched_disarm", _config.sched_disarm_time);

    // Auto-arm
    _prefs.putUShort("auto_arm_min", _config.auto_arm_minutes);

    // WiFi CSI runtime config
    _prefs.putBool  ("csi_en",     _config.csi_enabled);
    _prefs.putFloat ("csi_thr",    _config.csi_threshold);
    _prefs.putFloat ("csi_hyst",   _config.csi_hysteresis);
    _prefs.putUShort("csi_win",    _config.csi_window);
    _prefs.putUShort("csi_pubms",  _config.csi_publish_ms);
    _prefs.putBool  ("fus_en",     _config.fusion_enabled);
    _prefs.putBool  ("radar_bt",   _config.radar_bluetooth);
    _prefs.putString("csi_ssid",   _config.csi_ssid);
    _prefs.putString("csi_pass",   _config.csi_pass);

    DBG("CONFIG", "Configuration saved to NVS");
}

void ConfigManager::loadPref(const char* key, char* target, size_t maxLen) {
    String val = _prefs.getString(key, "");
    if (val.length() > 0) {
        val.toCharArray(target, maxLen);
    }
}
