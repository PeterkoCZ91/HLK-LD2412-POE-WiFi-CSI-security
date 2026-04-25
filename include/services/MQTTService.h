#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <atomic>
#include "secrets.h"
#include "services/MQTTOfflineBuffer.h"

#ifdef MQTTS_ENABLED
#if MQTTS_ENABLED == 1
#include <WiFiClientSecure.h>
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#endif
#endif

// Callback type for incoming MQTT commands
typedef void (*MQTTCommandCallback)(const char* topic, const char* payload);

struct MQTTTopics {
    char availability[64];
    char presence_state[64];
    char distance[64];
    char energy_mov[64];
    char energy_stat[64];
    char light[64];
    char tamper[64];
    char rssi[64];
    char uptime[64];
    char ip[64];
    char current_zone[64];
    char cmd_config[64];
    char system_error[64];
    char test_mode[64];

    // (Diagnostics published inline, no dedicated topics needed)

    // Engineering
    char eng_gate_base[64];

    // Advanced
    char motion_direction[64];
    char motion_type[64];
    char alarm_event[64];

    // Security alerts
    char alert_loitering[64];
    char alert_anti_masking[64];

    // Config command/state topics
    char cmd_max_range[64];
    char state_max_range[64];
    char cmd_hold_time[64];
    char state_hold_time[64];
    char cmd_sensitivity[64];
    char state_sensitivity[64];
    char cmd_pet_immunity[64];
    char state_pet_immunity[64];

    // LD2412 specific
    char cmd_dyn_bg[64];

    // Health & Statistics (TASK-008, TASK-009, TASK-010)
    char health_score[64];
    char frame_rate[64];
    char error_count[64];
    char uart_state[64];
    char free_heap[64];
    char max_alloc_heap[64];

    // Alarm control panel
    char alarm_state[64];
    char alarm_set[64];

    // Engineering mode status
    char eng_mode[64];

    // Identity
    char radar_type[64];

    // System restart diagnostics
    char restart_cause[64];

    // Chip temperature
    char chip_temp[64];

    // Supervision heartbeat
    char supervision_alive[64];

    // TIER 3 heartbeat (uptime payload, defeats HA dedup on unchanged diagnostics)
    char heartbeat[64];

    // Multi-sensor mesh
    char mesh_verify_request[64];
    char mesh_verify_confirm[64];

    // CSI Fusion
    char fusion_presence[64];
    char fusion_confidence[64];
    char fusion_source[64];
};

class MQTTService {
public:
    MQTTService();

    void begin(Preferences* prefs, const char* deviceId, const char* fwVersion = "unknown");
    void update();
    bool connected();
    bool publish(const char* topic, const char* payload, bool retained = false);
    unsigned long getLastPublishTime() const { return _lastPublish; }
    void forceReconnect();

    // Publish health diagnostics (v4.5.6)
    uint16_t getPublishFailStreak() const { return _publishFailStreak; }
    uint32_t getPublishFailTotal()  const { return _publishFailTotal; }
    uint32_t getReconnectTotal()    const { return _reconnectTotal; }
    unsigned long getLastPublishFailMs() const { return _lastPublishFailMs; }
    const char* getLastFailTopic()  const { return _lastFailTopic; }
    int  getLastFailState()         const { return _lastFailState; }
    
    void setCommandCallback(MQTTCommandCallback cb) { _commandCallback = cb; }
    void checkCertificateExpiry();
    void setOfflineBuffer(MQTTOfflineBuffer* buf) { _offlineBuffer = buf; }

    const MQTTTopics& getTopics() const { return _topics; }
    PubSubClient& getClient() { return _mqttClient; }
    const char* getServer() const { return _server; }
    const char* getPort() const { return _port; }
    bool consumeReconnect() { bool v = _justReconnected; _justReconnected = false; return v; }

    // halt all MQTT activity (loop + reconnect) while HTTP/ArduinoOTA
    // upload is in flight. Without this, a failing self-IP reconnect attempt
    // opens/closes lwIP sockets mid-chunk and disturbs the AsyncTCP receive
    // path, producing non-deterministic upload stalls.
    static void setOtaInProgress(bool active) { _otaInProgress.store(active); }
    static bool isOtaInProgress() { return _otaInProgress.load(); }

private:
    void setupClient();
    void connect();
    void generateTopics();
    void publishDiscoveryStep();
    void publishOneDiscovery(const char* type, const char* uid, const char* name, const char* state_topic, const char* unit, const char* icon, const char* dev_class, const char* extra);
    static void mqttCallbackStatic(char* topic, byte* payload, unsigned int length);
    void handleMessage(char* topic, byte* payload, unsigned int length);

    #ifdef MQTTS_ENABLED
    #if MQTTS_ENABLED == 1
    WiFiClientSecure _espClient;
    #else
    WiFiClient _espClient;
    #endif
    #else
    WiFiClient _espClient;
    #endif

    PubSubClient _mqttClient;
    MQTTTopics _topics;
    Preferences* _prefs;
    MQTTCommandCallback _commandCallback = nullptr;

    char _server[60];
    char _port[6];
    char _user[40];
    char _pass[40];
    char _deviceId[40];
    char _fwVersion[32];

    unsigned long _lastReconnectAttempt = 0;
    unsigned long _lastPublish = 0;
    unsigned long _reconnectInterval = 5000;      // Start at 5s, grows exponentially
    unsigned long _maxReconnectInterval = 300000;  // Cap at 5 min

    // Non-blocking HA Discovery state machine
    int _discoveryIndex = -1;  // -1 = not in progress
    String _devInfo;           // Cached device info JSON fragment
    bool _justReconnected = false;

    MQTTOfflineBuffer* _offlineBuffer = nullptr;

    // Publish health tracking (v4.5.6)
    uint16_t _publishFailStreak = 0;
    uint32_t _publishFailTotal  = 0;
    uint32_t _reconnectTotal    = 0;
    unsigned long _lastPublishFailMs = 0;
    char _lastFailTopic[64] = {0};
    int  _lastFailState = 0;

    static MQTTService* _instance;
    static std::atomic<bool> _otaInProgress;
};

#endif
