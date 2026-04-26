#ifndef LITE_BUILD
#include "services/MQTTService.h"
#include "debug.h"
#include "constants.h"
#include <ETH.h>

MQTTService* MQTTService::_instance = nullptr;
std::atomic<bool> MQTTService::_otaInProgress{false};

MQTTService::MQTTService() : _mqttClient(_espClient) {
    _instance = this;
}

void MQTTService::begin(Preferences* prefs, const char* deviceId, const char* fwVersion) {
    _prefs = prefs;
    strncpy(_deviceId, deviceId, sizeof(_deviceId) - 1);
    _deviceId[sizeof(_deviceId) - 1] = '\0';
    strncpy(_fwVersion, fwVersion, sizeof(_fwVersion) - 1);
    _fwVersion[sizeof(_fwVersion) - 1] = '\0';

    String s_server = _prefs->getString("mqtt_server", MQTT_SERVER_DEFAULT);
    String s_port = _prefs->getString("mqtt_port", "");
    String s_user = _prefs->getString("mqtt_user", MQTT_USER_DEFAULT);
    String s_pass = _prefs->getString("mqtt_pass", MQTT_PASS_DEFAULT);

    s_server.toCharArray(_server, sizeof(_server));
    s_port.toCharArray(_port, sizeof(_port));
    s_user.toCharArray(_user, sizeof(_user));
    s_pass.toCharArray(_pass, sizeof(_pass));

    _lastPublish = millis(); // Prevent DMS false trigger before first publish

    generateTopics();
    setupClient();
}

void MQTTService::generateTopics() {
    snprintf(_topics.availability,    sizeof(_topics.availability),    "security/%s/availability", _deviceId);
    snprintf(_topics.presence_state,  sizeof(_topics.presence_state),  "security/%s/presence/state", _deviceId);
    snprintf(_topics.distance,        sizeof(_topics.distance),        "security/%s/presence/distance", _deviceId);
    snprintf(_topics.energy_mov,      sizeof(_topics.energy_mov),      "security/%s/presence/energy_mov", _deviceId);
    snprintf(_topics.energy_stat,     sizeof(_topics.energy_stat),     "security/%s/presence/energy_stat", _deviceId);
    snprintf(_topics.light,           sizeof(_topics.light),           "security/%s/presence/light", _deviceId);
    snprintf(_topics.tamper,          sizeof(_topics.tamper),          "security/%s/tamper", _deviceId);
    snprintf(_topics.rssi,            sizeof(_topics.rssi),            "security/%s/rssi", _deviceId);
    snprintf(_topics.uptime,          sizeof(_topics.uptime),          "security/%s/uptime", _deviceId);
    snprintf(_topics.ip,              sizeof(_topics.ip),              "security/%s/ip", _deviceId);
    snprintf(_topics.current_zone,    sizeof(_topics.current_zone),    "security/%s/presence/zone", _deviceId);
    snprintf(_topics.cmd_config,      sizeof(_topics.cmd_config),      "security/%s/config/set", _deviceId);
    snprintf(_topics.system_error,    sizeof(_topics.system_error),    "security/%s/system/error", _deviceId);
    snprintf(_topics.test_mode,       sizeof(_topics.test_mode),       "security/%s/test_mode", _deviceId);


    snprintf(_topics.eng_gate_base,   sizeof(_topics.eng_gate_base),   "security/%s/engineering/gate", _deviceId);

    snprintf(_topics.motion_direction,sizeof(_topics.motion_direction),"security/%s/presence/direction", _deviceId);
    snprintf(_topics.motion_type,     sizeof(_topics.motion_type),     "security/%s/presence/motion_type", _deviceId);
    snprintf(_topics.alarm_event,     sizeof(_topics.alarm_event),     "security/%s/alarm/event", _deviceId);
    snprintf(_topics.alert_loitering, sizeof(_topics.alert_loitering), "security/%s/alert/loitering", _deviceId);
    snprintf(_topics.alert_anti_masking, sizeof(_topics.alert_anti_masking), "security/%s/alert/anti_masking", _deviceId);

    snprintf(_topics.cmd_max_range,     sizeof(_topics.cmd_max_range),     "security/%s/config/max_range/set", _deviceId);
    snprintf(_topics.state_max_range,   sizeof(_topics.state_max_range),   "security/%s/config/max_range", _deviceId);
    snprintf(_topics.cmd_hold_time,     sizeof(_topics.cmd_hold_time),     "security/%s/config/hold_time/set", _deviceId);
    snprintf(_topics.state_hold_time,   sizeof(_topics.state_hold_time),   "security/%s/config/hold_time", _deviceId);
    snprintf(_topics.cmd_sensitivity,   sizeof(_topics.cmd_sensitivity),   "security/%s/config/sensitivity/set", _deviceId);
    snprintf(_topics.state_sensitivity, sizeof(_topics.state_sensitivity), "security/%s/config/sensitivity", _deviceId);
    snprintf(_topics.cmd_pet_immunity,  sizeof(_topics.cmd_pet_immunity),  "security/%s/config/pet_immunity/set", _deviceId);
    snprintf(_topics.state_pet_immunity,sizeof(_topics.state_pet_immunity),"security/%s/config/pet_immunity", _deviceId);

    snprintf(_topics.cmd_dyn_bg,      sizeof(_topics.cmd_dyn_bg),      "security/%s/dyn_bg/set", _deviceId);

    // Health & Statistics (TASK-008, TASK-009, TASK-010)
    snprintf(_topics.health_score,    sizeof(_topics.health_score),    "security/%s/system/health_score", _deviceId);
    snprintf(_topics.frame_rate,      sizeof(_topics.frame_rate),      "security/%s/system/frame_rate", _deviceId);
    snprintf(_topics.error_count,     sizeof(_topics.error_count),     "security/%s/system/error_count", _deviceId);
    snprintf(_topics.uart_state,      sizeof(_topics.uart_state),      "security/%s/system/uart_state", _deviceId);
    snprintf(_topics.free_heap,       sizeof(_topics.free_heap),       "security/%s/system/free_heap", _deviceId);
    snprintf(_topics.max_alloc_heap,  sizeof(_topics.max_alloc_heap),  "security/%s/system/max_alloc_heap", _deviceId);

    // Alarm control panel
    snprintf(_topics.alarm_state,     sizeof(_topics.alarm_state),     "security/%s/alarm/state", _deviceId);
    snprintf(_topics.alarm_set,       sizeof(_topics.alarm_set),       "security/%s/alarm/set", _deviceId);

    // Engineering mode status
    snprintf(_topics.eng_mode,        sizeof(_topics.eng_mode),        "security/%s/system/eng_mode", _deviceId);

    // Identity
    snprintf(_topics.radar_type,      sizeof(_topics.radar_type),      "security/%s/radar_type", _deviceId);

    // System restart diagnostics
    snprintf(_topics.restart_cause,   sizeof(_topics.restart_cause),   "security/%s/system/restart_cause", _deviceId);
    snprintf(_topics.chip_temp,       sizeof(_topics.chip_temp),       "security/%s/system/chip_temp", _deviceId);

    // Supervision heartbeat
    snprintf(_topics.supervision_alive, sizeof(_topics.supervision_alive), "security/%s/supervision/alive", _deviceId);

    // TIER 3 heartbeat (unique-per-tick uptime payload — defeats HA dedup)
    snprintf(_topics.heartbeat, sizeof(_topics.heartbeat), "security/%s/heartbeat", _deviceId);

    // Multi-sensor mesh
    snprintf(_topics.mesh_verify_request, sizeof(_topics.mesh_verify_request), "security/%s/mesh/verify_request", _deviceId);
    snprintf(_topics.mesh_verify_confirm, sizeof(_topics.mesh_verify_confirm), "security/%s/mesh/verify_confirm", _deviceId);

    // CSI Fusion
    snprintf(_topics.fusion_presence,   sizeof(_topics.fusion_presence),   "security/%s/fusion/presence", _deviceId);
    snprintf(_topics.fusion_confidence, sizeof(_topics.fusion_confidence), "security/%s/fusion/confidence", _deviceId);
    snprintf(_topics.fusion_source,     sizeof(_topics.fusion_source),     "security/%s/fusion/source", _deviceId);
}

void MQTTService::setupClient() {
    if (strlen(_server) == 0) return;

    int portInt;
    if (strlen(_port) > 0) {
        portInt = atoi(_port);
    } else {
        #ifdef MQTTS_ENABLED
        #if MQTTS_ENABLED == 1
        portInt = MQTTS_PORT;
        #else
        portInt = MQTT_PORT_DEFAULT;
        #endif
        #else
        portInt = MQTT_PORT_DEFAULT;
        #endif
    }

    #ifdef MQTTS_ENABLED
    #if MQTTS_ENABLED == 1
    _espClient.setCACert(mqtt_server_ca);
    DBG("MQTT", "MQTTS (TLS) enabled");
    #else
    DBG("MQTT", "Plain MQTT");
    #endif
    #else
    DBG("MQTT", "Plain MQTT");
    #endif

    _mqttClient.setServer(_server, portInt);
    _mqttClient.setBufferSize(2048);
    _mqttClient.setCallback(mqttCallbackStatic);

    DBG("MQTT", "Server: %s, Port: %d", _server, portInt);
}

void MQTTService::mqttCallbackStatic(char* topic, byte* payload, unsigned int length) {
    if (_instance) {
        _instance->handleMessage(topic, payload, length);
    }
}

void MQTTService::handleMessage(char* topic, byte* payload, unsigned int length) {
    char msg[256];
    size_t len = (length < sizeof(msg) - 1) ? length : sizeof(msg) - 1;
    memcpy(msg, payload, len);
    msg[len] = '\0';

    DBG("MQTT", "Received: %s -> %s", topic, msg);

    if (_commandCallback) {
        _commandCallback(topic, msg);
    }
}

void MQTTService::update() {
    if (strlen(_server) == 0) return;
    // csi10w: freeze MQTT entirely during HTTP/ArduinoOTA upload. PubSubClient
    // uses blocking WiFiClient.connect() which opens lwIP sockets; a failing
    // reconnect mid-OTA (e.g. broker unreachable) starves AsyncTCP of receive
    // bandwidth and produces non-deterministic upload stalls.
    if (_otaInProgress.load()) return;

    if (!_mqttClient.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = now;
            connect();
        }
    } else {
        _mqttClient.loop();

        // Non-blocking discovery: publish one entity per update() cycle
        if (_discoveryIndex >= 0) {
            publishDiscoveryStep();
        }
    }
}

void MQTTService::connect() {
    DBG("MQTT", "Connecting to %s...", _server);

    if (_mqttClient.connect(_deviceId, _user, _pass, _topics.availability, 1, true, "offline")) {
        DBG("MQTT", "Connected!");
        _lastPublish = millis();  // Reset DMS timer on connect (prevents overflow loop)
        _mqttClient.publish(_topics.availability, "online", true);
        _mqttClient.publish(_topics.radar_type, "ld2412", true);

        // Subscribe to all command topics
        _mqttClient.subscribe(_topics.cmd_config);
        _mqttClient.subscribe(_topics.cmd_max_range);
        _mqttClient.subscribe(_topics.cmd_hold_time);
        _mqttClient.subscribe(_topics.cmd_sensitivity);
        _mqttClient.subscribe(_topics.cmd_pet_immunity);
        _mqttClient.subscribe(_topics.cmd_dyn_bg);
        _mqttClient.subscribe(_topics.alarm_set);
        _mqttClient.subscribe("security/+/supervision/alive");
        _mqttClient.subscribe("security/+/mesh/verify_request");
        _mqttClient.subscribe("security/+/mesh/verify_confirm");

        // Publish IP
        _mqttClient.publish(_topics.ip, ETH.localIP().toString().c_str(), true);

        _reconnectInterval = 5000; // Reset backoff on success
        _justReconnected = true;
        _publishFailStreak = 0;
        _reconnectTotal++;

        // Replay any messages buffered while offline
        if (_offlineBuffer && _offlineBuffer->count() > 0) {
            _offlineBuffer->replay([this](const char* t, const char* p, bool r) {
                return _mqttClient.publish(t, p, r);
            });
        }

        // Start non-blocking discovery (one entity per update() cycle)
        char devInfoBuf[256];
        snprintf(devInfoBuf, sizeof(devInfoBuf),
            "{\"ids\":\"%s\",\"name\":\"LD2412 %s\",\"mdl\":\"ESP32+LD2412\",\"mf\":\"HiLink\",\"sw\":\"%s\"}",
            _deviceId, _deviceId, _fwVersion);
        _devInfo = devInfoBuf;
        _discoveryIndex = 0;
    } else {
        // Exponential backoff: 5s → 10s → 20s → 40s → ... → 300s max
        _reconnectInterval = min(_reconnectInterval * 2, _maxReconnectInterval);
        DBG("MQTT", "Failed, rc=%d (next retry in %lus)", _mqttClient.state(), _reconnectInterval / 1000);
    }
}

bool MQTTService::connected() {
    return _mqttClient.connected();
}

void MQTTService::forceReconnect() {
    DBG("MQTT", "Force reconnect: disconnecting...");
    _mqttClient.disconnect();
    _reconnectInterval = 5000;
    _lastReconnectAttempt = 0;
    delay(100);
    connect();
}

bool MQTTService::publish(const char* topic, const char* payload, bool retained) {
    if (!_mqttClient.connected()) {
        if (_offlineBuffer) _offlineBuffer->store(topic, payload, retained);
        return false;
    }
    bool success = _mqttClient.publish(topic, payload, retained);
    if (success) {
        _lastPublish = millis();
        _publishFailStreak = 0;
        return true;
    }
    // publish() returned false while connected()==true → broker or socket stuck.
    // Track fail streak, capture diagnostics, buffer for replay, and force a
    // clean reconnect once streak exceeds the limit (pre-DMS recovery).
    _publishFailStreak++;
    _publishFailTotal++;
    _lastPublishFailMs = millis();
    _lastFailState = _mqttClient.state();
    strncpy(_lastFailTopic, topic, sizeof(_lastFailTopic) - 1);
    _lastFailTopic[sizeof(_lastFailTopic) - 1] = '\0';
    if (_offlineBuffer) _offlineBuffer->store(topic, payload, retained);
    if (_publishFailStreak >= MQTT_PUBLISH_FAIL_STREAK_LIMIT) {
        DBG("MQTT", "Publish fail streak %u on '%s' (state=%d) — forcing disconnect",
            _publishFailStreak, topic, _lastFailState);
        _mqttClient.disconnect();
        _publishFailStreak = 0;
    }
    return false;
}

// Publish a single HA Discovery entity (called from publishDiscoveryStep)
void MQTTService::publishOneDiscovery(const char* type, const char* uid, const char* name, const char* state_topic, const char* unit, const char* icon, const char* dev_class, const char* extra) {
    JsonDocument doc;
    doc["name"] = name;

    char uniq[64];
    snprintf(uniq, sizeof(uniq), "%s_%s", _deviceId, uid);
    doc["uniq_id"] = uniq;
    doc["stat_t"] = state_topic;
    if (strlen(unit) > 0) doc["unit_of_meas"] = unit;
    if (strlen(icon) > 0) doc["ic"] = icon;
    if (strlen(dev_class) > 0) doc["dev_cla"] = dev_class;
    doc["avty_t"] = _topics.availability;
    doc["dev"] = serialized(_devInfo);

    if (extra && strlen(extra) > 0) {
        JsonDocument extraDoc;
        deserializeJson(extraDoc, extra);
        JsonObject root = extraDoc.as<JsonObject>();
        for (JsonPair kv : root) { doc[kv.key()] = kv.value(); }
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/%s/%s_%s/config", type, _deviceId, uid);
    String payload;
    serializeJson(doc, payload);

    if (_mqttClient.publish(topic, payload.c_str(), true)) {
        DBG("MQTT", "Disc SENT: %s", topic);
    } else {
        DBG("MQTT", "Disc FAIL: %s (Buffer/Net Error)", topic);
    }
}

// Non-blocking state machine: publishes one HA Discovery entity per call
void MQTTService::publishDiscoveryStep() {
    if (!_mqttClient.connected() || _discoveryIndex < 0) {
        _discoveryIndex = -1;
        return;
    }

    if (_discoveryIndex == 0) {
        DBG("MQTT", "Publishing HA Discovery (non-blocking)...");
    }

    switch (_discoveryIndex) {
        case 0:  publishOneDiscovery("binary_sensor", "presence", "Presence", _topics.presence_state, "", "mdi:motion-sensor", "motion", "{\"pl_on\":\"detected\",\"pl_off\":\"idle\"}"); break;
        case 1:  publishOneDiscovery("binary_sensor", "tamper", "Sabotage (Tamper)", _topics.tamper, "", "mdi:shield-alert", "tamper", "{\"pl_on\":\"true\",\"pl_off\":\"false\"}"); break;
        case 2:  publishOneDiscovery("sensor", "dist", "Distance", _topics.distance, "cm", "mdi:ruler", "distance", ""); break;
        case 3:  publishOneDiscovery("sensor", "mov_energy", "Moving Energy", _topics.energy_mov, "%", "mdi:run", "", "{\"ent_cat\":\"diagnostic\"}"); break;
        case 4:  publishOneDiscovery("sensor", "stat_energy", "Static Energy", _topics.energy_stat, "%", "mdi:motion-pause", "", "{\"ent_cat\":\"diagnostic\"}"); break;
        case 5:  publishOneDiscovery("sensor", "light", "Light Level", _topics.light, "lx", "mdi:brightness-5", "illuminance", ""); break;
        case 6:  publishOneDiscovery("binary_sensor", "eth_link", "ETH Link", _topics.rssi, "", "mdi:ethernet", "connectivity", "{\"ent_cat\":\"diagnostic\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\"}"); break;
        case 7:  publishOneDiscovery("sensor", "uptime", "Uptime", _topics.uptime, "s", "mdi:clock-outline", "", "{\"ent_cat\":\"diagnostic\"}"); break;
        case 8:  publishOneDiscovery("sensor", "mdir", "Motion Direction", _topics.motion_direction, "", "mdi:arrow-decision", "", ""); break;
        case 9:  publishOneDiscovery("sensor", "zone", "Current Zone", _topics.current_zone, "", "mdi:map-marker-radius", "", ""); break;
        case 10: {
            char bgExtra[128];
            snprintf(bgExtra, sizeof(bgExtra), "{\"cmd_t\":\"%s\",\"ent_cat\":\"config\"}", _topics.cmd_dyn_bg);
            publishOneDiscovery("button", "dyn_bg", "Dynamic BG Correction", "", "", "mdi:auto-fix", "", bgExtra);
            break;
        }
        case 11: {
            char holdExtra[256];
            snprintf(holdExtra, sizeof(holdExtra), "{\"cmd_t\":\"%s\",\"min\":500,\"max\":30000,\"step\":500,\"ent_cat\":\"config\"}", _topics.cmd_hold_time);
            publishOneDiscovery("number", "hold_time", "Hold Time (ms)", _topics.state_hold_time, "ms", "mdi:timer-sand", "", holdExtra);
            break;
        }
        case 12: publishOneDiscovery("binary_sensor", "loitering", "Loitering Alert", _topics.alert_loitering, "", "mdi:account-clock", "occupancy", "{\"pl_on\":\"true\",\"pl_off\":\"false\"}"); break;
        case 13: publishOneDiscovery("binary_sensor", "anti_masking", "Anti-Masking (Blind)", _topics.alert_anti_masking, "", "mdi:eye-off", "problem", "{\"pl_on\":\"true\",\"pl_off\":\"false\"}"); break;
        case 14: publishOneDiscovery("sensor", "health_score", "Health Score", _topics.health_score, "%", "mdi:heart-pulse", "", "{\"ent_cat\":\"diagnostic\"}"); break;
        case 15: publishOneDiscovery("sensor", "frame_rate", "Frame Rate", _topics.frame_rate, "fps", "mdi:speedometer", "", "{\"ent_cat\":\"diagnostic\"}"); break;
        case 16: publishOneDiscovery("sensor", "error_count", "Error Count", _topics.error_count, "", "mdi:alert-circle", "", "{\"ent_cat\":\"diagnostic\"}"); break;
        case 17: publishOneDiscovery("sensor", "uart_state", "UART State", _topics.uart_state, "", "mdi:serial-port", "", "{\"ent_cat\":\"diagnostic\"}"); break;
        case 18: publishOneDiscovery("sensor", "free_heap", "Free Memory", _topics.free_heap, "KB", "mdi:memory", "", "{\"ent_cat\":\"diagnostic\"}"); break;
        case 19: {
            char maxAllocTopic[128];
            snprintf(maxAllocTopic, sizeof(maxAllocTopic), "security/%s/system/max_alloc_heap", _deviceId);
            publishOneDiscovery("sensor", "max_alloc", "Max Alloc Heap", maxAllocTopic, "KB", "mdi:memory-arrow-up", "", "{\"ent_cat\":\"diagnostic\"}");
            break;
        }
        case 20: {
            char alarmExtra[256];
            snprintf(alarmExtra, sizeof(alarmExtra),
                "{\"cmd_t\":\"%s\",\"stat_t\":\"%s\",\"sup_feat\":[\"arm_away\"]}",
                _topics.alarm_set, _topics.alarm_state);
            publishOneDiscovery("alarm_control_panel", "alarm", "Security Alarm", _topics.alarm_state, "", "mdi:shield-home", "", alarmExtra);
            break;
        }
        case 21: publishOneDiscovery("binary_sensor", "eng_mode", "Engineering Mode", _topics.eng_mode, "", "mdi:chip", "", "{\"pl_on\":\"true\",\"pl_off\":\"false\",\"ent_cat\":\"diagnostic\"}"); break;
        case 22: publishOneDiscovery("sensor", "restart_cause", "Restart Cause", _topics.restart_cause, "", "mdi:restart-alert", "", "{\"ent_cat\":\"diagnostic\"}"); break;
        case 23: publishOneDiscovery("sensor", "motion_type", "Motion Type", _topics.motion_type, "", "mdi:motion-sensor", "", ""); break;
        case 24: publishOneDiscovery("sensor", "chip_temp", "Chip Temperature", _topics.chip_temp, "°C", "mdi:thermometer", "temperature", "{\"ent_cat\":\"diagnostic\"}"); break;
        default: {
            // Engineering gates: indices 25-52 (14 gates × 2: moving + static)
            int gateOffset = _discoveryIndex - 25;
            if (gateOffset >= 0 && gateOffset < 28) {
                int gateNum = gateOffset / 2;
                bool isMoving = (gateOffset % 2 == 0);
                char topic[96], uid[20], name[32];
                if (isMoving) {
                    snprintf(topic, sizeof(topic), "%s%d/moving", _topics.eng_gate_base, gateNum);
                    snprintf(uid, sizeof(uid), "gate%d_mov", gateNum);
                    snprintf(name, sizeof(name), "Gate %d Moving", gateNum);
                } else {
                    snprintf(topic, sizeof(topic), "%s%d/static", _topics.eng_gate_base, gateNum);
                    snprintf(uid, sizeof(uid), "gate%d_stat", gateNum);
                    snprintf(name, sizeof(name), "Gate %d Static", gateNum);
                }
                publishOneDiscovery("sensor", uid, name, topic, "%", "mdi:radar", "", "{\"ent_cat\":\"diagnostic\"}");
            } else {
                // All entities published
                DBG("MQTT", "HA Discovery complete (%d entities)", _discoveryIndex);
                _discoveryIndex = -1;
                return;
            }
            break;
        }
    }

    _discoveryIndex++;
}

void MQTTService::checkCertificateExpiry() {
    #ifdef MQTTS_ENABLED
    #if MQTTS_ENABLED == 1

    // NTP already configured in setup() - do not reconfigure here

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        DBG("Cert", "Time not synced. Skipping check.");
        return;
    }

    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);

    int ret = mbedtls_x509_crt_parse(&cert, (const unsigned char*)mqtt_server_ca, strlen(mqtt_server_ca) + 1);
    if (ret != 0) {
        DBG("Cert", "Parse failed: -0x%x", -ret);
        mbedtls_x509_crt_free(&cert);
        return;
    }

    struct tm exp_tm = {0};
    exp_tm.tm_year = cert.valid_to.year - 1900;
    exp_tm.tm_mon  = cert.valid_to.mon - 1;
    exp_tm.tm_mday = cert.valid_to.day;
    exp_tm.tm_hour = cert.valid_to.hour;
    exp_tm.tm_min  = cert.valid_to.min;
    exp_tm.tm_sec  = cert.valid_to.sec;

    time_t exp_time = mktime(&exp_tm);
    time_t now = time(nullptr);
    double days_left = difftime(exp_time, now) / 86400.0;

    DBG("Cert", "Expires: %04d-%02d-%02d. Days left: %.1f",
                  cert.valid_to.year, cert.valid_to.mon, cert.valid_to.day, days_left);

    if (days_left < 30) {
        DBG("Cert", "WARNING: Certificate expires soon!");
        if (_mqttClient.connected()) {
            char msg[80];
            snprintf(msg, sizeof(msg), "MQTT certificate expires in %d days", (int)days_left);
            _mqttClient.publish(_topics.system_error, msg, true);
        }
    }

    mbedtls_x509_crt_free(&cert);

    #endif
    #endif
}
#endif
