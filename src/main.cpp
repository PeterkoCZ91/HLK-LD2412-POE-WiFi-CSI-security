#include <Arduino.h>
#include <ETH.h>
#ifndef LITE_BUILD
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#endif
#include <ArduinoJson.h>
#ifndef LITE_BUILD
#include <ArduinoOTA.h>
#endif
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <atomic>
#ifndef LITE_BUILD
#include <HTTPClient.h>
#endif

#include "services/LD2412Service.h"
#include "services/MQTTService.h"
#include "services/SecurityMonitor.h"
#include "services/NotificationService.h"
#include "services/TelegramService.h"
#include "services/LogService.h"
#include "services/EventLog.h"
#include "services/ConfigSnapshot.h"
#include "services/MQTTOfflineBuffer.h"
#ifndef NO_BLUETOOTH
#include "services/BluetoothService.h"
#endif
#ifdef USE_CSI
#include "services/CSIService.h"
#endif
#include "debug.h"
#include "secrets.h"
#include "constants.h"
#include "ConfigManager.h"
#include "WebRoutes.h"
#include <esp_ota_ops.h>
#include <ESPmDNS.h>
#include <time.h>

// -------------------------------------------------------------------------
// Defines
// -------------------------------------------------------------------------
#include <Update.h>
#ifndef FW_VERSION
#define FW_VERSION "v5.0.2"
#endif
#define WDT_TIMEOUT_SECONDS 60

// v5.0.2-rc1: RTC slow-memory uptime tracker for finer-grained TWDT crash forensics.
// Hourly NVS save quantizes reset_history uptimes to 1h/2h/5h boundaries — useless for
// pattern detection. RTC mem survives Task WDT / panic / SW reset (cleared only on
// power-on / brownout), so updating it every loop tick gives ~1s resolution on crash
// time without NVS wear. Magic guards against uninitialized RTC junk after power-on.
RTC_DATA_ATTR uint32_t rtc_uptime_magic = 0;
RTC_DATA_ATTR uint32_t rtc_last_uptime_s = 0;
constexpr uint32_t RTC_UPTIME_MAGIC = 0xDECAFBAD;

// NTP Config
const char* ntpServer = "pool.ntp.org";

// Radar UART pins - defined in platformio.ini
#ifndef RADAR_RX_PIN
#error "RADAR_RX_PIN not defined! Use correct environment: esp32_poe"
#endif
#ifndef RADAR_TX_PIN
#error "RADAR_TX_PIN not defined! Use correct environment: esp32_poe"
#endif

#define LED_PIN 2  // USER LED on Prokyber ESP32-STICK

// Radar OUT pin (hardware detection output) - optional
#ifndef RADAR_OUT_PIN
#define RADAR_OUT_PIN -1
#endif

// Siren/strobe GPIO output - optional
#ifndef SIREN_PIN
#define SIREN_PIN SIREN_PIN_DEFAULT
#endif

// -------------------------------------------------------------------------
// Objects
// -------------------------------------------------------------------------
LD2412Service radar(RADAR_RX_PIN, RADAR_TX_PIN);
#ifndef LITE_BUILD
AsyncWebServer server(80);
AsyncEventSource events("/events");
#endif
Preferences preferences;
MQTTService mqttService;
SecurityMonitor securityMonitor;
#ifdef USE_CSI
CSIService csiService;
#endif
NotificationService notificationService;
TelegramService telegramBot;
LogService systemLog(20);
EventLog eventLog(RAM_CAPACITY);
ConfigSnapshot configSnapshot;
MQTTOfflineBuffer mqttOfflineBuffer;
#ifndef NO_BLUETOOTH
BluetoothService btService;
#endif
TaskHandle_t radarTaskHandle = nullptr;
String g_prevRestartCause = "none";

// -------------------------------------------------------------------------
// Supervision Heartbeat — peer monitoring
// -------------------------------------------------------------------------
struct SupervisionPeer {
    char id[32];
    unsigned long lastSeen;  // millis()
    bool alerted;            // tamper alert already sent for this peer
};
static constexpr uint8_t MAX_PEERS = 8;
static SupervisionPeer peers[MAX_PEERS];
static uint8_t peerCount = 0;
static unsigned long lastSupervisionPublish = 0;
static constexpr unsigned long SUPERVISION_INTERVAL_MS = 60000;   // Publish every 60s
static constexpr unsigned long SUPERVISION_TIMEOUT_MS  = 180000;  // Alert after 3x interval (3 min)
static const char* g_myDeviceId = nullptr; // Set in setup() after configManager init

// -------------------------------------------------------------------------
// Multi-sensor mesh — cross-node alarm verification
// -------------------------------------------------------------------------
static bool meshVerifyPending = false;        // We sent a verify request, awaiting confirms
static unsigned long meshVerifyRequestTime = 0;
static uint8_t meshConfirmCount = 0;
static constexpr unsigned long MESH_VERIFY_TIMEOUT_MS = 5000; // 5s window for peer confirms

void supervisionPeerSeen(const char* peerId) {
    if (g_myDeviceId && strcmp(peerId, g_myDeviceId) == 0) return;

    unsigned long now = millis();
    for (uint8_t i = 0; i < peerCount; i++) {
        if (strcmp(peers[i].id, peerId) == 0) {
            if (peers[i].alerted) {
                DBG("SUPV", "Peer '%s' back online", peerId);
                peers[i].alerted = false;
            }
            peers[i].lastSeen = now;
            return;
        }
    }
    if (peerCount < MAX_PEERS) {
        strncpy(peers[peerCount].id, peerId, sizeof(peers[peerCount].id) - 1);
        peers[peerCount].id[sizeof(peers[peerCount].id) - 1] = '\0';
        peers[peerCount].lastSeen = now;
        peers[peerCount].alerted = false;
        peerCount++;
        DBG("SUPV", "New peer discovered: '%s' (total: %d)", peerId, peerCount);
    }
}

void supervisionCheck() {
    unsigned long now = millis();
    for (uint8_t i = 0; i < peerCount; i++) {
        if (!peers[i].alerted && now - peers[i].lastSeen > SUPERVISION_TIMEOUT_MS) {
            peers[i].alerted = true;
            DBG("SUPV", "PEER OFFLINE: '%s' (no heartbeat for %lus)", peers[i].id, (now - peers[i].lastSeen) / 1000);
            String msg = "🔴 SUPERVISION: Node '" + String(peers[i].id) + "' offline!";
            String details = "No heartbeat for " + String((now - peers[i].lastSeen) / 1000) + "s. Possible tamper or failure.";
            notificationService.sendAlert(NotificationType::TAMPER_ALERT, msg, details);
            if (mqttService.connected()) {
                mqttService.publish(mqttService.getTopics().tamper, "peer_offline", false);
            }
        }
    }
}

// -------------------------------------------------------------------------
// Config
// -------------------------------------------------------------------------
ConfigManager configManager;

String zonesJson = "[]";

void saveZonesToNVS();
void loadZonesFromNVS();

bool shouldSaveConfig = false;
volatile bool shouldReboot = false;
bool bootValidated = false;

// Reboot inhibit. When ON, every soft restart path (safeRestart, DMS phase 2,
// ETH watchdog, /api/restart) is suppressed and only logged. OTA success sets
// g_otaRebootForce so slot swap still completes regardless. NVS key
// "reboot_inhibit" persists the choice across reboots.
std::atomic<bool> g_rebootInhibit{false};
std::atomic<bool> g_otaRebootForce{false};

static String pendingZonesJson = "";
static volatile bool pendingZonesUpdate = false;
SemaphoreHandle_t zonesMutex = NULL;

unsigned long lastLedBlink = 0;
unsigned long lastTele = 0;
unsigned long bootTime = 0;

// ETH connection state
static volatile bool ethConnected = false;
static volatile bool ethGotIP = false;

// ETH link restore notification (set by connectivityTask, consumed by loop)
static volatile bool ethLinkRestoredNotify = false;
static volatile unsigned long ethLinkDownSeconds = 0;

// -------------------------------------------------------------------------
// Publish-on-Change Tracking
// -------------------------------------------------------------------------
struct LastPublished {
    char presence_state[16] = "";
    bool tamper = false, anti_masking = false, loitering = false;
    char alarm_state[16] = "";
    char motion_type[8] = "";

    uint16_t distance_cm = 0;
    uint8_t energy_mov = 0, energy_stat = 0;
    char direction[16] = "";

    uint32_t uptime_s = 0;
    uint8_t health_score = 0;
    float frame_rate = 0.0f;
    uint32_t error_count = 0;
    char uart_state[24] = "";
    uint32_t free_heap_kb = 0, max_alloc_kb = 0;
    bool eng_mode = false;

    uint8_t gate_mov[14] = {0}, gate_stat[14] = {0};
    uint8_t light_level = 0;

    unsigned long lastDiagPublish = 0;
    unsigned long lastEngPublish = 0;
    unsigned long lastTempPublish = 0;
    float chip_temp = -99.0f;
    unsigned long lastTempAlert = 0;
    bool tempAlertActive = false;
    unsigned long lastHeapAlert = 0;
    bool heapAlertActive = false;

    // Fusion
    bool fusion_presence = false;
    float fusion_confidence = -1.0f;
    char fusion_source[12] = "";  // csi8b extended 3-bit mask: "radar+csi", "csi+ml", "all" (max 9 chars + null)
};
static LastPublished lastPub;

static inline bool changedU16(uint16_t c, uint16_t l, uint16_t d) { return (c > l+d) || (l > c+d); }
static inline bool changedU8 (uint8_t  c, uint8_t  l, uint8_t  d) { return (c > l+d) || (l > c+d); }
static inline bool changedU32(uint32_t c, uint32_t l, uint32_t d) { return (c > l+d) || (l > c+d); }
static inline bool changedF  (float    c, float    l, float    d) { float diff=c-l; return diff>d||diff<-d; }

#include "web_interface.h"
#include "known_devices.h"

// -------------------------------------------------------------------------
// Ethernet Event Handler
// -------------------------------------------------------------------------
void onEthEvent(arduino_event_id_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            Serial.println("[ETH] Started");
            ETH.setHostname(configManager.getConfig().hostname);
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("[ETH] Link UP");
            ethConnected = true;
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.printf("[ETH] IP: %s  Speed: %dMbps  Duplex: %s\n",
                ETH.localIP().toString().c_str(),
                ETH.linkSpeed(),
                ETH.fullDuplex() ? "Full" : "Half");
            ethGotIP = true;
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("[ETH] Link DOWN");
            ethConnected = false;
            ethGotIP = false;
            break;
        case ARDUINO_EVENT_ETH_STOP:
            Serial.println("[ETH] Stopped");
            ethConnected = false;
            ethGotIP = false;
            break;
        default:
            break;
    }
}

// -------------------------------------------------------------------------
// Safe Restart
// -------------------------------------------------------------------------
void safeRestart(const char* reason) {
    bool force = g_otaRebootForce.load();
    if (g_rebootInhibit.load() && !force) {
        DBG("SYSTEM", ">>> REBOOT INHIBIT: '%s' suppressed (uptime %lus, heap %u)",
            reason, millis() / 1000, ESP.getFreeHeap());
        systemLog.warn(String("Reboot inhibit: ") + reason + " suppressed");
        return;
    }
    preferences.putString("restart_cause", reason);
    preferences.putULong("last_uptime", millis() / 1000);
    preferences.putULong("last_heap", ESP.getFreeHeap());
    preferences.putULong("last_maxalloc", ESP.getMaxAllocHeap());
    preferences.putULong("last_minheap", ESP.getMinFreeHeap());
    DBG("SYSTEM", ">>> RESTART: %s (uptime %lus, heap %u/%u/%u)",
        reason, millis() / 1000, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
    delay(500);
    ESP.restart();
}

// -------------------------------------------------------------------------
// Zones Persistence
// -------------------------------------------------------------------------
void updateZonesFromJSON() {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, zonesJson);
    if (error) {
        DBG("CONFIG", "Failed to parse zones JSON");
        return;
    }

    std::vector<AlertZone> zones;
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        AlertZone z;
        String name = obj["name"] | "Zone";
        strncpy(z.name, name.c_str(), sizeof(z.name)-1);
        z.name[sizeof(z.name)-1] = '\0';
        z.min_cm = obj["min"] | 0;
        z.max_cm = obj["max"] | 0;
        z.alert_level = obj["level"] | 0;
        z.delay_ms = obj["delay"] | 0;
        z.enabled = obj["enabled"] | true;
        z.alarm_behavior = obj["alarm_behavior"] | 0;

        String prevZone = obj["prev_zone"] | "";
        strncpy(z.valid_prev_zone, prevZone.c_str(), sizeof(z.valid_prev_zone)-1);
        z.valid_prev_zone[sizeof(z.valid_prev_zone)-1] = '\0';

        zones.push_back(z);
    }
    securityMonitor.setZones(zones);
    DBG("CONFIG", "Updated %d zones", zones.size());
}

void saveZonesToNVS() {
    if (zonesJson.length() < 1000) {
        preferences.putString("zones_json", zonesJson);
        DBG("CONFIG", "Zones saved to NVS");
        updateZonesFromJSON();
    }
}

void loadZonesFromNVS() {
    if (preferences.isKey("zones_json")) {
        zonesJson = preferences.getString("zones_json", "[]");
        DBG("CONFIG", "Zones loaded from NVS: %d bytes", zonesJson.length());
        updateZonesFromJSON();
    }
}

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------
void radarTask(void* param) {
    const TickType_t delayTicks = pdMS_TO_TICKS(2);
    for (;;) {
        radar.update();
        vTaskDelay(delayTicks);
    }
}

// ETH link watchdog timeout — reboot if link stays down this long
constexpr unsigned long ETH_LINK_WATCHDOG_MS = 300000; // 5 minut

// Connectivity watchdog — monitors ETH link + MQTT health
void connectivityTask(void* param) {
    const TickType_t delayTicks = pdMS_TO_TICKS(INTERVAL_CONNECTIVITY_MS);
    int mqttFailCount = 0;
    const int MQTT_FAIL_THRESHOLD = 5;
    unsigned long ethDownSince = 0; // 0 = ETH is up

    for (;;) {
        vTaskDelay(delayTicks);

        // Skip the watchdog cycle entirely while a /api/update is in flight.
        // The watchdog otherwise prints DBG, hits ETH.linkUp() / mqtt state,
        // and (without inhibit) could reboot mid-upload.
#ifdef USE_CSI
        if (CSIService::isOtaInProgress()) continue;
#endif

        // --- ETH link watchdog ---
        if (!ETH.linkUp()) {
            if (ethDownSince == 0) {
                ethDownSince = millis();
                DBG("CONN", "ETH link DOWN — watchdog started");
                systemLog.warn("ETH link DOWN");
            } else {
                unsigned long downFor = millis() - ethDownSince;
                DBG("CONN", "ETH link DOWN for %lu s / %lu s timeout",
                    downFor / 1000, ETH_LINK_WATCHDOG_MS / 1000);

                if (downFor >= ETH_LINK_WATCHDOG_MS) {
                    if (g_rebootInhibit.load()) {
                        // Avoid log-spam each loop: throttle to once / minute
                        static unsigned long lastInhibitLog = 0;
                        if (millis() - lastInhibitLog > 60000) {
                            lastInhibitLog = millis();
                            systemLog.warn("ETH link down >5min, reboot inhibit ON — staying up");
                        }
                    } else {
                        systemLog.error("ETH link down > 5min — rebooting");
                        safeRestart("eth_link_lost");
                    }
                }
            }
            mqttFailCount = 0;
            continue;
        }

        // ETH is up — reset watchdog
        if (ethDownSince != 0) {
            unsigned long downSec = (millis() - ethDownSince) / 1000;
            DBG("CONN", "ETH link restored after %lu s", downSec);
            systemLog.info("ETH link restored after " + String(downSec) + "s");
            ethLinkDownSeconds = downSec;
            ethLinkRestoredNotify = true; // consumed by loop()
            ethDownSince = 0;
        }

        // --- MQTT watchdog ---
        if (mqttService.connected()) {
            mqttFailCount = 0;
        } else {
            mqttFailCount++;
            DBG("CONN", "ETH OK but MQTT down (%d/%d)", mqttFailCount, MQTT_FAIL_THRESHOLD);
            if (mqttFailCount >= MQTT_FAIL_THRESHOLD) {
                DBG("CONN", "MQTT down too long — will reconnect on next loop");
                mqttFailCount = 0;
            }
        }
    }
}

void setup() {
    Serial.begin(115200);

    zonesMutex = xSemaphoreCreateMutex();
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // --- Factory Reset: hold GPIO 0 (BOOT button) for 5 seconds at boot ---
    // NOTE: Some USB-serial bridges (CP210x) hold GPIO 0 LOW via DTR after reset.
    // Wait 2s for USB-serial lines to stabilize before checking button state.
    pinMode(0, INPUT_PULLUP);
    delay(2000);
    if (digitalRead(0) == LOW) {
        unsigned long pressStart = millis();
        Serial.println("[SYSTEM] GPIO 0 pressed — hold 5s for factory reset...");
        digitalWrite(LED_PIN, HIGH);
        while (digitalRead(0) == LOW && (millis() - pressStart) < 5000) {
            delay(100);
        }
        if (millis() - pressStart >= 5000) {
            Serial.println("[SYSTEM] FACTORY RESET! Clearing NVS...");
            Preferences resetPrefs;
            resetPrefs.begin("radar_config", false);
            resetPrefs.clear();
            resetPrefs.end();
            Serial.println("[SYSTEM] NVS cleared. Restarting...");
            for (int i = 0; i < 6; i++) {
                digitalWrite(LED_PIN, i % 2); delay(200);
            }
            ESP.restart();
        }
        digitalWrite(LED_PIN, LOW);
        Serial.println("[SYSTEM] GPIO 0 released — normal boot.");
    }

    // Initialize radar OUT pin if connected
    #if RADAR_OUT_PIN >= 0
    pinMode(RADAR_OUT_PIN, INPUT);
    DBG("INIT", "Radar OUT pin: GPIO %d", RADAR_OUT_PIN);
    #endif

    delay(500);
    Serial.println("\n\n\n");
    Serial.println("=============================================");
    Serial.println("   POE-2412 SECURITY NODE - BOOT SEQUENCE");
    Serial.println("=============================================");
    Serial.print(">> FW Version: "); Serial.println(FW_VERSION);

    configManager.begin();

    Serial.println("---------------------------------------------");
    Serial.printf("[SYSTEM] MAC: %s\n", ETH.macAddress().c_str());
    Serial.printf("[SYSTEM] MQTT Server: %s\n", configManager.getConfig().mqtt_server);
    Serial.printf("[SYSTEM] MQTT Client ID: %s\n", configManager.getConfig().mqtt_id);
    Serial.println("---------------------------------------------");

    preferences.begin("radar_config", false);

    // Reboot inhibit: dev build default ON. Persisted choice wins on subsequent
    // boots so toggling via /api/reboot_inhibit survives reboots within the
    // same firmware build.
    {
        bool persisted = preferences.getBool("reboot_inhibit", true);
        g_rebootInhibit.store(persisted);
        Serial.printf("[SYSTEM] Reboot inhibit: %s (NVS)\n", persisted ? "ON" : "OFF");
    }

    Serial.print(">> MQTT Broker Target: "); Serial.println(configManager.getConfig().mqtt_server);
    Serial.println("=============================================\n");

    // Register ETH event handler BEFORE ETH.begin()
    WiFi.onEvent(onEthEvent);

    // --- Auto-Config by MAC (Multi-Device Support) ---
    // Note: ETH MAC available after begin(), so we re-check after ETH init
    // For now use ETH.macAddress() which may be empty before begin() on some boards
    // Known-device lookup happens after ETH.begin() below

    // Load zones from NVS
    loadZonesFromNVS();

    // Load Pet Immunity
    uint8_t petImmunity = preferences.getUInt("sec_pet", 0);
    if (petImmunity > 0) {
        radar.setMinMoveEnergy(petImmunity);
        securityMonitor.setPetImmunity(petImmunity);
        DBG("CONFIG", "Pet Immunity loaded: %d", petImmunity);
    }

    // Load Hold Time
    unsigned long holdTime = preferences.getULong("hold_time", 500);
    radar.setHoldTime(holdTime);
    DBG("CONFIG", "Hold Time loaded: %lu ms", holdTime);

    // --- Reset Reason Logging ---
    esp_reset_reason_t reason = esp_reset_reason();
    String reasonStr;
    switch (reason) {
        case ESP_RST_POWERON: reasonStr = "Power-on"; break;
        case ESP_RST_EXT:     reasonStr = "External pin"; break;
        case ESP_RST_SW:      reasonStr = "Software reset"; break;
        case ESP_RST_PANIC:   reasonStr = "Exception/Panic"; break;
        case ESP_RST_INT_WDT: reasonStr = "Interrupt WDT"; break;
        case ESP_RST_TASK_WDT: reasonStr = "Task WDT"; break;
        case ESP_RST_WDT:      reasonStr = "Other WDT"; break;
        case ESP_RST_DEEPSLEEP: reasonStr = "Deep sleep"; break;
        case ESP_RST_BROWNOUT: reasonStr = "Brownout"; break;
        case ESP_RST_SDIO:     reasonStr = "SDIO reset"; break;
        default:               reasonStr = "Unknown"; break;
    }

    String history = preferences.getString("reset_history", "[]");
    JsonDocument historyDoc;
    deserializeJson(historyDoc, history);
    JsonArray arr = historyDoc.as<JsonArray>();

    String prevRestartCause = preferences.getString("restart_cause", "none");
    g_prevRestartCause = prevRestartCause;

    // v5.0.2-rc1: prefer RTC slow-memory uptime (1s resolution, survives TWDT/panic/SW)
    // over NVS last_uptime (1h resolution, survives power-on too). Magic check rejects
    // uninitialized RTC junk after first power-on.
    uint32_t crashUptime = preferences.getULong("last_uptime", 0);
    bool rtcValid = (rtc_uptime_magic == RTC_UPTIME_MAGIC);
    if (rtcValid && rtc_last_uptime_s > 0) {
        crashUptime = rtc_last_uptime_s;
    }
    rtc_uptime_magic = RTC_UPTIME_MAGIC;  // arm for next crash
    rtc_last_uptime_s = 0;

    JsonObject entry = arr.add<JsonObject>();
    entry["reason"] = reasonStr;
    entry["cause"] = prevRestartCause;
    entry["uptime"] = crashUptime;
    entry["uptime_src"] = rtcValid ? "rtc" : "nvs";
    entry["ts"] = millis();

    while (arr.size() > 10) arr.remove(0);

    String newHistory;
    serializeJson(historyDoc, newHistory);
    preferences.putString("reset_history", newHistory);
    preferences.putString("restart_cause", "none");
    DBG("SYSTEM", "Reset reason: %s, cause: %s", reasonStr.c_str(), prevRestartCause.c_str());
    systemLog.warn("System restart: " + reasonStr + " (" + prevRestartCause + ")");

    uint8_t minGate = preferences.getUInt("radar_min", 0);
    uint8_t maxGate = preferences.getUInt("radar_max", 13);

    if (!radar.begin(Serial2, minGate, maxGate)) {
        Serial.println("[RADAR] Failed to init LD2412");
        systemLog.error("Radar init failed");
    } else {
        systemLog.info("Radar initialized");

        float storedRes = configManager.getConfig().radar_resolution;
        if (storedRes != 0.75f) {
            if (radar.setResolution(storedRes)) {
                DBG("RADAR", "Resolution set to %.2fm", storedRes);
            } else {
                DBG("RADAR", "Resolution command failed (%.2fm)", storedRes);
            }
        } else {
            DBG("RADAR", "Resolution: 0.75m (default, skipping command)");
        }

        // Enforce radar Bluetooth policy — deployed units must not accept HLK
        // app pairing unless the operator opted in via config. Idempotent: only
        // calls the radar if current state differs from desired.
        bool wantBt = configManager.getConfig().radar_bluetooth;
        if (radar.applyBluetoothState(wantBt)) {
            systemLog.info(String("Radar BT ") + (wantBt ? "enabled" : "disabled"));
        } else {
            systemLog.warn("Radar BT policy apply failed");
        }
    }

    // Run radar update in a dedicated task on core 1
    xTaskCreatePinnedToCore(radarTask, "radar_task", 8192, nullptr, 2, &radarTaskHandle, 1);

    // --- Start Ethernet ---
    Serial.println("[ETH] Initializing LAN8720A...");
    ETH.begin(ETH_PHY_ADDR, -1, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_TYPE, ETH_CLK_MODE);

    // Apply static IP if configured
    if (strlen(configManager.getConfig().static_ip) > 0) {
        IPAddress ip, gw, mask, dns;
        if (ip.fromString(configManager.getConfig().static_ip) &&
            gw.fromString(configManager.getConfig().static_gw)) {
            mask.fromString(configManager.getConfig().static_mask);
            if (strlen(configManager.getConfig().static_dns) > 0) {
                dns.fromString(configManager.getConfig().static_dns);
            } else {
                dns = gw; // fallback: gateway as DNS
            }
            ETH.config(ip, gw, mask, dns);
            Serial.printf("[ETH] Static IP: %s  GW: %s  DNS: %s\n",
                ip.toString().c_str(), gw.toString().c_str(), dns.toString().c_str());
        } else {
            Serial.println("[ETH] Invalid static IP config — using DHCP");
        }
    } else {
        Serial.println("[ETH] Using DHCP");
    }

    // Wait for IP (max 15 seconds)
    unsigned long ethStart = millis();
    while (!ethGotIP && millis() - ethStart < 15000) {
        delay(100);
    }

    if (!ethGotIP) {
        Serial.println("[ETH] No IP after 15s — continuing without network");
        systemLog.warn("Ethernet: no IP at boot");
    } else {
        Serial.printf("[ETH] Ready — IP: %s\n", ETH.localIP().toString().c_str());
    }

    // NOTE: LAN8720A PHY LEDs (LINK/ACT, SPEED) are hardware-driven by PHY internal state.
    // They cannot be disabled via MDIO registers while ETH link is active.
    // Only the User LED (GPIO2) is software-controllable via led_en setting.

    // --- Auto-Config by MAC (after ETH.begin so MAC is valid) ---
    String mac = ETH.macAddress();
    mac.toLowerCase();
    DBG("SETUP", "MAC: %s", mac.c_str());

    bool knownDevice = false;
    for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
        if (mac == KNOWN_DEVICES[i].mac) {
            DBG("SETUP", "Match found! Auto-configuring as: %s", KNOWN_DEVICES[i].id);
            strncpy(configManager.getConfig().mqtt_id, KNOWN_DEVICES[i].id, 39);
            configManager.getConfig().mqtt_id[39] = '\0';
            if (preferences.getString("mqtt_id", "") != String(KNOWN_DEVICES[i].id)) {
                preferences.putString("mqtt_id", KNOWN_DEVICES[i].id);
            }
            // Hostname: NVS has priority, KNOWN_DEVICES is just default for first boot
            String nvsHostname = preferences.getString("hostname", "");
            if (nvsHostname.isEmpty()) {
                nvsHostname = KNOWN_DEVICES[i].hostname;
                preferences.putString("hostname", nvsHostname);
            }
            strncpy(configManager.getConfig().hostname, nvsHostname.c_str(), 32);
            configManager.getConfig().hostname[32] = '\0';
            ETH.setHostname(nvsHostname.c_str());
            knownDevice = true;
            break;
        }
    }
    if (!knownDevice) {
        DBG("SETUP", "Unknown device. Using stored/default config.");
    }

    DBG("NET", "IP: %s  GW: %s  SN: %s",
        ETH.localIP().toString().c_str(),
        ETH.gatewayIP().toString().c_str(),
        ETH.subnetMask().toString().c_str());

    // Init mDNS
    if (MDNS.begin(configManager.getConfig().hostname)) {
        MDNS.addService("http", "tcp", 80);
        DBG("NET", "mDNS started: %s.local", configManager.getConfig().hostname);
    }

    // Init NTP (timezone from config)
    configTime(configManager.getConfig().tz_offset, configManager.getConfig().dst_offset, ntpServer);
    DBG("SYSTEM", "NTP Time sync started (tz=%d, dst=%d)",
        configManager.getConfig().tz_offset, configManager.getConfig().dst_offset);

    // Init SSE
#ifndef LITE_BUILD
    events.onConnect([](AsyncEventSourceClient *client){
        if (client->lastId()) {
            DBG("SSE", "Client reconnected! Last message ID: %u", client->lastId());
        }
        client->send("hello!", NULL, millis(), 1000);
    });
    server.addHandler(&events);
#endif

    // WDT after ETH init
    esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
    esp_task_wdt_add(NULL);

    // --- Web Server Routes ---
#ifndef LITE_BUILD
    WebRoutes::Dependencies deps = {
        .server = &server,
        .events = &events,
        .preferences = &preferences,
        .radar = &radar,
        .mqttService = &mqttService,
        .securityMonitor = &securityMonitor,
        .telegramBot = &telegramBot,
        .notificationService = &notificationService,
        .systemLog = &systemLog,
        .eventLog = &eventLog,
        #ifndef NO_BLUETOOTH
        .bluetooth = &btService,
        #else
        .bluetooth = nullptr,
        #endif
        .config = &configManager.getConfig(),
        .configManager = &configManager,
        .zonesJson = &zonesJson,
        .fwVersion = FW_VERSION,
        .shouldReboot = &shouldReboot,
        .pendingZonesJson = &pendingZonesJson,
        .pendingZonesUpdate = &pendingZonesUpdate,
        .zonesMutex = &zonesMutex,
        .configSnapshot = &configSnapshot,
        #ifdef USE_CSI
        .csiService = &csiService,
        #else
        .csiService = nullptr,
        #endif
    };

    WebRoutes::setup(deps);
    server.begin();
#endif

    // Init services
    g_myDeviceId = configManager.getConfig().mqtt_id; // For supervision heartbeat
    if (configManager.getConfig().mqtt_enabled) {
        mqttService.begin(&preferences, configManager.getConfig().mqtt_id, FW_VERSION);
        mqttService.setCommandCallback([](const char* topic, const char* payload) {
            const MQTTTopics& t = mqttService.getTopics();
            if (strcmp(topic, t.cmd_max_range) == 0) {
                int val = atoi(payload);
                if (val >= 1 && val <= 14) radar.setParamConfig(1, (uint8_t)val, 5);
            } else if (strcmp(topic, t.cmd_hold_time) == 0) {
                unsigned long val = strtoul(payload, nullptr, 10);
                if (val <= 65535) {
                    radar.setHoldTime(val);
                    preferences.putULong("hold_time", val);
                    mqttService.publish(t.state_hold_time, String(val).c_str(), true);
                }
            } else if (strcmp(topic, t.cmd_sensitivity) == 0) {
                int a, b, c;
                int n = sscanf(payload, "%d,%d,%d", &a, &b, &c);
                if (n == 2 && a >= 0 && a <= 100 && b >= 0 && b <= 100) {
                    radar.setMotionSensitivity((uint8_t)a);
                    radar.setStaticSensitivity((uint8_t)b);
                } else if (n == 3 && a >= 0 && a <= 13 && b >= 0 && b <= 100 && c >= 0 && c <= 100) {
                    const uint8_t* currentMov = radar.getMotionSensitivityArray();
                    const uint8_t* currentStat = radar.getStaticSensitivityArray();
                    uint8_t movArr[14], statArr[14];
                    memcpy(movArr, currentMov, 14);
                    memcpy(statArr, currentStat, 14);
                    movArr[a] = b; statArr[a] = c;
                    radar.setMotionSensitivity(movArr);
                    radar.setStaticSensitivity(statArr);
                }
            } else if (strcmp(topic, t.cmd_pet_immunity) == 0) {
                int val = atoi(payload);
                if (val >= 0 && val <= 100) {
                    radar.setMinMoveEnergy((uint8_t)val);
                    securityMonitor.setPetImmunity((uint8_t)val);
                    preferences.putUInt("sec_pet", (uint8_t)val);
                }
            } else if (strcmp(topic, t.cmd_dyn_bg) == 0) {
                radar.startCalibration();
            } else if (strcmp(topic, t.alarm_set) == 0) {
                String cmd = String(payload);
                if (cmd == "ARM_AWAY") securityMonitor.setArmed(true, false, false);
                else if (cmd == "ARM_HOME") securityMonitor.setArmed(true, false, true);
                else if (cmd == "DISARM") securityMonitor.setArmed(false);
            } else if (strstr(topic, "/supervision/alive") != nullptr) {
                const char* start = topic + 9; // skip "security/"
                const char* end = strstr(start, "/supervision");
                if (end && end - start < 32) {
                    char peerId[32];
                    size_t len = end - start;
                    memcpy(peerId, start, len);
                    peerId[len] = '\0';
                    supervisionPeerSeen(peerId);
                }
            } else if (strstr(topic, "/mesh/verify_request") != nullptr) {
                auto d = radar.getData();
                if (d.distance_cm > 0 && (d.moving_energy > 0 || d.static_energy > 0)) {
                    char confirmTopic[96];
                    snprintf(confirmTopic, sizeof(confirmTopic), "security/%s/mesh/verify_confirm", g_myDeviceId);
                    char confirmPayload[64];
                    snprintf(confirmPayload, sizeof(confirmPayload), "{\"dist\":%d,\"mov\":%d,\"stat\":%d}",
                        d.distance_cm, d.moving_energy, d.static_energy);
                    mqttService.publish(confirmTopic, confirmPayload, false);
                    DBG("MESH", "Confirmed verify request (dist=%d)", d.distance_cm);
                }
            } else if (strstr(topic, "/mesh/verify_confirm") != nullptr) {
                if (meshVerifyPending) {
                    meshConfirmCount++;
                    DBG("MESH", "Received verify confirm #%d", meshConfirmCount);
                }
            }
        });
    } else {
        DBG("SYSTEM", "MQTT Disabled (Stand-alone Mode)");
    }

    notificationService.begin(&preferences, configManager.getConfig().mqtt_id);
    telegramBot.begin(&preferences);
    telegramBot.setRadarService(&radar);
    notificationService.setTelegramService(&telegramBot);

    // Mount LittleFS once — shared by EventLog, TelemetryBuffer, ConfigSnapshot, web assets
    bool fsOk = LittleFS.begin(false);
    if (!fsOk) {
        Serial.println("[FS] LittleFS mount failed — formatting...");
        fsOk = LittleFS.begin(true);
        if (!fsOk) Serial.println("[FS] LittleFS format FAILED — persistence disabled");
        else Serial.println("[FS] LittleFS formatted (previous data lost)");
    }
    if (fsOk) DBG("FS", "LittleFS mounted — %u KB used / %u KB total", LittleFS.usedBytes()/1024, LittleFS.totalBytes()/1024);

    eventLog.begin(fsOk);
    if (fsOk) configSnapshot.begin();
    if (fsOk && configManager.getConfig().mqtt_enabled) {
        mqttOfflineBuffer.begin();
        mqttService.setOfflineBuffer(&mqttOfflineBuffer);
    }
    securityMonitor.begin(&notificationService, &mqttService, &telegramBot, &eventLog, &preferences, configManager.getConfig().mqtt_id);
    telegramBot.setSecurityMonitor(&securityMonitor);
    telegramBot.setRebootFlag(&shouldReboot);

    // Load security config from NVS
    if (preferences.isKey("sec_antimask"))
        securityMonitor.setAntiMaskTime(preferences.getULong("sec_antimask", DEFAULT_ANTI_MASK_MS));

    securityMonitor.setAntiMaskEnabled(preferences.getBool("sec_am_en", false));

    if (preferences.isKey("sec_loiter"))
        securityMonitor.setLoiterTime(preferences.getULong("sec_loiter", 15000));
    if (preferences.isKey("sec_loit_en"))
        securityMonitor.setLoiterAlertEnabled(preferences.getBool("sec_loit_en", true));
    if (preferences.isKey("sec_hb"))
        securityMonitor.setHeartbeatInterval(preferences.getULong("sec_hb", 14400000));

    securityMonitor.setEntryDelay(preferences.getULong("sec_entry_dl", DEFAULT_ENTRY_DELAY_MS));
    securityMonitor.setExitDelay(preferences.getULong("sec_exit_dl", DEFAULT_EXIT_DELAY_MS));
    securityMonitor.setDisarmReminderEnabled(preferences.getBool("sec_dis_rem", false));
    securityMonitor.setTriggerTimeout(preferences.getULong("sec_trig_to", DEFAULT_TRIGGER_TIMEOUT_MS));
    securityMonitor.setAutoRearm(preferences.getBool("sec_auto_rearm", true));
    securityMonitor.setAlarmEnergyThreshold(preferences.getUChar("sec_alarm_en", DEFAULT_ALARM_ENERGY_THRESHOLD));
    securityMonitor.setSirenPin(SIREN_PIN);
    if (preferences.getBool("sec_armed", false)) {
        bool homeMode = preferences.getBool("sec_home_mode", false);
        securityMonitor.setArmed(true, false, homeMode);
    }

#ifndef LITE_BUILD
    ArduinoOTA.onStart([]() {
        CSIService::setOtaInProgress(true);
        MQTTService::setOtaInProgress(true);
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else
            type = "filesystem";
        Serial.println("Start updating " + type);
        // Save config snapshot before OTA overwrites flash
        configSnapshot.saveSnapshot(&preferences, FW_VERSION, "ota_arduino");
        if (radarTaskHandle) vTaskSuspend(radarTaskHandle);
        radar.stop();
        if (telegramBot.isEnabled()) {
            Serial.println("[OTA] Attempting Telegram notification...");
            telegramBot.sendMessage("⚠️ OTA Update Started...");
        }
    });
    ArduinoOTA.onEnd([]() {
        CSIService::setOtaInProgress(false);
        MQTTService::setOtaInProgress(false);
    });
    ArduinoOTA.onError([](ota_error_t) {
        CSIService::setOtaInProgress(false);
        MQTTService::setOtaInProgress(false);
    });

    ArduinoOTA.setPassword(configManager.getConfig().auth_pass);
    ArduinoOTA.begin();
#endif

    bootTime = millis();
    Serial.printf("[SYSTEM] POE-2412 Security Node %s ready\n", FW_VERSION);

    // Start background connectivity watchdog
    xTaskCreatePinnedToCore(connectivityTask, "conn_check", 3072, nullptr, 1, nullptr, 0);

    if (telegramBot.isEnabled()) {
        String bootMsg = "🟢 *POE-2412 Security Node Online*\n";
        bootMsg += "🏷️ Device: " + String(configManager.getConfig().mqtt_id) + "\n";
        bootMsg += "FW: " + String(FW_VERSION) + "\n";
        bootMsg += "IP: " + ETH.localIP().toString() + "\n";
        if (g_prevRestartCause == "eth_link_lost") {
            bootMsg += "⚠️ *Restart reason: ETH link lost >5min*";
        } else if (g_prevRestartCause != "none") {
            bootMsg += "ℹ️ Restart: " + g_prevRestartCause;
        }
        telegramBot.sendMessage(bootMsg);
    }

#ifdef USE_CSI
    // WiFi CSI: uses WiFi STA purely for CSI packet capture (network stays on Ethernet)
    // Runtime gate via csi_enabled (NVS) — even if compiled in, user can disable in GUI
    if (configManager.getConfig().csi_enabled) {
        // Apply NVS-stored runtime config BEFORE begin so allocations use right window size
        csiService.setWindowSize(configManager.getConfig().csi_window);
        csiService.setThreshold(configManager.getConfig().csi_threshold);
        csiService.setHysteresis(configManager.getConfig().csi_hysteresis);
        csiService.setPublishInterval(configManager.getConfig().csi_publish_ms);

        // Traffic generator tuning from NVS
        if (preferences.isKey("csi_tport")) csiService.setTrafficPort(preferences.getUShort("csi_tport", 7));
        if (preferences.isKey("csi_ticmp")) csiService.setTrafficICMP(preferences.getBool("csi_ticmp", false));
        if (preferences.isKey("csi_tpps"))  csiService.setTrafficRate(preferences.getUInt("csi_tpps", 100));

        // csi9: ML MLP runtime settings from NVS (defaults: enabled=true, threshold=0.50)
        if (preferences.isKey("csi_ml_en"))  csiService.setMlEnabled(preferences.getBool("csi_ml_en", true));
        if (preferences.isKey("csi_ml_thr")) csiService.setMlThreshold(preferences.getFloat("csi_ml_thr", 0.50f));

        // csi10c: prefer NVS-stored SSID/pass (GUI-editable) over compile-time defaults
        const SystemConfig& cfg = configManager.getConfig();
        const char* effSsid = (cfg.csi_ssid[0] != '\0') ? cfg.csi_ssid : CSI_WIFI_SSID;
        const char* effPass = (cfg.csi_ssid[0] != '\0') ? cfg.csi_pass : CSI_WIFI_PASS;
        csiService.begin(effSsid, effPass, &mqttService,
                         (String(cfg.mqtt_id) + "/csi").c_str(),
                         &preferences);
        if (configManager.getConfig().fusion_enabled) {
            securityMonitor.setCSISource(&csiService);
            DBG("CSI", "Fusion enabled — CSI linked to SecurityMonitor");
        } else {
            DBG("CSI", "Fusion disabled in config — CSI runs independently");
        }
    } else {
        Serial.println("[CSI] disabled in NVS — skipping begin()");
    }
#endif
}

// -------------------------------------------------------------------------
// Loop
// -------------------------------------------------------------------------
void loop() {
    unsigned long now = millis();
    esp_task_wdt_reset();

    // v5.0.2-rc1: RTC uptime tracker — every loop tick. Survives Task WDT/panic
    // so reset_history gets ~1s resolution on crash time instead of hourly.
    rtc_last_uptime_s = now / 1000;

#ifndef LITE_BUILD
    ArduinoOTA.handle();
#endif

    if (shouldReboot) {
        if (g_rebootInhibit.load() && !g_otaRebootForce.load()) {
            DBG("SYSTEM", "shouldReboot=true but inhibit ON — clearing");
            systemLog.warn("Manual reboot suppressed by inhibit");
            shouldReboot = false;
        } else {
            safeRestart(g_otaRebootForce.load() ? "ota_complete" : "manual_reboot");
        }
    }

    // Process pending zones update from async web handler
    if (pendingZonesUpdate) {
        if (zonesMutex != NULL && xSemaphoreTake(zonesMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            zonesJson = pendingZonesJson;
            pendingZonesUpdate = false;
            xSemaphoreGive(zonesMutex);
            saveZonesToNVS();
        }
    }

    if (configManager.getConfig().mqtt_enabled) mqttService.update();
#ifdef USE_CSI
    if (configManager.getConfig().csi_enabled) {
        // csi6b: ground-truth presence from LD2412 radar — keeps stationary humans
        // (CSI variance low, breathing hold active) out of quiet-site learning.
        RadarData csiRadarGate = radar.getData();
        csiService.notePresenceFromRadar(csiRadarGate.valid &&
                                         csiRadarGate.state != PresenceState::IDLE);
        csiService.update();
    }
#endif

    // Force full MQTT state replay after reconnect (broker restart, retained loss)
    if (mqttService.consumeReconnect()) {
        memset(lastPub.presence_state, 0, sizeof(lastPub.presence_state));
        memset(lastPub.alarm_state, 0, sizeof(lastPub.alarm_state));
        memset(lastPub.direction, 0, sizeof(lastPub.direction));
        memset(lastPub.motion_type, 0, sizeof(lastPub.motion_type));
        lastPub.tamper = !lastPub.tamper;
        lastPub.anti_masking = !lastPub.anti_masking;
        lastPub.loitering = !lastPub.loitering;
        lastPub.eng_mode = !lastPub.eng_mode;
        lastPub.distance_cm = 0xFFFF;
        lastPub.energy_mov = 0xFF;
        lastPub.energy_stat = 0xFF;
        // TIER 3 diagnostics — addresses Codex ESP↔HA link check report 2026-04-21:
        // after MQTT reconnect, stable values (health_score=100, uart_state=RUNNING, etc.)
        // would stay gated by deadband forever. Invalidate so next publish re-fires them.
        lastPub.health_score  = 0xFF;
        lastPub.frame_rate    = -1.0f;
        lastPub.error_count   = 0xFFFFFFFFUL;
        memset(lastPub.uart_state, 0, sizeof(lastPub.uart_state));
        lastPub.free_heap_kb  = 0;
        lastPub.max_alloc_kb  = 0;
        DBG("MQTT", "Reconnect detected — forcing full state replay");
    }

    // Publish restart cause once after first MQTT connection
    static bool restartCausePublished = false;
    if (!restartCausePublished && mqttService.connected()) {
        restartCausePublished = true;
        const MQTTTopics& t = mqttService.getTopics();
        esp_reset_reason_t r = esp_reset_reason();
        const char* rStr = "unknown";
        switch (r) {
            case ESP_RST_POWERON: rStr = "power_on"; break;
            case ESP_RST_SW:      rStr = "sw_reset"; break;
            case ESP_RST_PANIC:   rStr = "panic"; break;
            case ESP_RST_INT_WDT: rStr = "int_wdt"; break;
            case ESP_RST_TASK_WDT: rStr = "task_wdt"; break;
            case ESP_RST_WDT:      rStr = "wdt"; break;
            case ESP_RST_BROWNOUT: rStr = "brownout"; break;
            default: break;
        }
        String msg = String(rStr) + ":" + g_prevRestartCause;
        // Append heap snapshot from previous run (if safeRestart was used)
        uint32_t prevHeap = preferences.getULong("last_heap", 0);
        uint32_t prevMaxAlloc = preferences.getULong("last_maxalloc", 0);
        uint32_t prevMinHeap = preferences.getULong("last_minheap", 0);
        if (prevHeap > 0) {
            msg += "|heap:" + String(prevHeap) + "/" + String(prevMaxAlloc) + "/" + String(prevMinHeap);
        }
        mqttService.publish(t.restart_cause, msg.c_str(), true);
        DBG("SYSTEM", "Published restart cause: %s", msg.c_str());
    }

    // One-shot gate config verification 40s after boot (ESPHome #13366: V1.26 may revert UART config)
    {
        static bool gateVerified = false;
        if (!gateVerified && now - bootTime >= TIMEOUT_GATE_VERIFY_MS) {
            gateVerified = true;
            uint8_t expectedMin = preferences.getUInt("radar_min", 0);
            uint8_t expectedMax = preferences.getUInt("radar_max", 13);
            if (!radar.verifyGateConfig(expectedMin, expectedMax)) {
                eventLog.addEvent(EVT_SYSTEM, 0, 0, "Gate config reverted by FW");
            }
        }
    }

    securityMonitor.update();
    telegramBot.update();
    eventLog.flush();

    // ETH link restore notification (flag set by connectivityTask)
    if (ethLinkRestoredNotify) {
        ethLinkRestoredNotify = false;
        if (telegramBot.isEnabled()) {
            telegramBot.sendMessage("📶 ETH link restored\n⏱️ Outage lasted: " + String(ethLinkDownSeconds) + "s");
        }
    }

    // Scheduled arm/disarm (check every 30s).
    // Track the last day-of-year we acted on each side so a 30 s tick that
    // lands at HH:MM:30 still arms (was: tight curMinutes == armMinutes match
    // missed late ticks). The day field resets the latch at midnight so we
    // act once per day per direction.
    {
        static unsigned long lastSchedCheck = 0;
        static int lastArmYday = -1;
        static int lastDisarmYday = -1;
        if (now - lastSchedCheck > 30000) {
            lastSchedCheck = now;
            time_t epoch = time(nullptr);
            if (epoch > 1700000000) { // valid NTP time
                struct tm timeinfo;
                localtime_r(&epoch, &timeinfo);
                int curMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
                int curYday = timeinfo.tm_yday;
                const char* armTime = configManager.getConfig().sched_arm_time;
                const char* disarmTime = configManager.getConfig().sched_disarm_time;
                int armH, armM, disH, disM;
                if (strlen(armTime) >= 4 && sscanf(armTime, "%d:%d", &armH, &armM) == 2 &&
                    armH >= 0 && armH < 24 && armM >= 0 && armM < 60) {
                    int armMinutes = armH * 60 + armM;
                    if (curMinutes == armMinutes && lastArmYday != curYday &&
                        !securityMonitor.isArmed()) {
                        securityMonitor.setArmed(true);
                        lastArmYday = curYday;
                        DBG("SCHED", "Auto-armed at %s", armTime);
                        systemLog.info("Scheduled arm at " + String(armTime));
                        if (telegramBot.isEnabled()) {
                            telegramBot.sendMessage("🔒 Auto-armed (" + String(armTime) + ")");
                        }
                    }
                }
                if (strlen(disarmTime) >= 4 && sscanf(disarmTime, "%d:%d", &disH, &disM) == 2 &&
                    disH >= 0 && disH < 24 && disM >= 0 && disM < 60) {
                    int disMinutes = disH * 60 + disM;
                    if (curMinutes == disMinutes && lastDisarmYday != curYday &&
                        securityMonitor.isArmed()) {
                        securityMonitor.setArmed(false);
                        lastDisarmYday = curYday;
                        DBG("SCHED", "Auto-disarmed at %s", disarmTime);
                        systemLog.info("Scheduled disarm at " + String(disarmTime));
                        if (telegramBot.isEnabled()) {
                            telegramBot.sendMessage("🔓 Auto-disarmed (" + String(disarmTime) + ")");
                        }
                    }
                }
            }
        }
    }

    // Auto-arm after N minutes of no presence
    {
        static unsigned long lastPresenceTime = now; // reset on boot
        static bool wasArmed = false;
        RadarData peekData = radar.getData();

        // Any presence resets the idle timer
        if (peekData.state != PresenceState::IDLE) {
            lastPresenceTime = now;
        }

        bool armed = securityMonitor.isArmed();

        // Detect manual disarm → reset timer so auto-arm waits full interval
        if (wasArmed && !armed) {
            lastPresenceTime = now;
            DBG("AUTO-ARM", "Manual disarm detected — timer reset");
        }
        wasArmed = armed;

        uint16_t autoArmMin = configManager.getConfig().auto_arm_minutes;
        if (autoArmMin > 0 && !armed) {
            unsigned long elapsed = (now - lastPresenceTime) / 60000; // minutes
            if (elapsed >= autoArmMin) {
                securityMonitor.setArmed(true);
                wasArmed = true;
                lastPresenceTime = now; // prevent re-trigger if immediately disarmed
                DBG("AUTO-ARM", "No presence for %u min — armed", autoArmMin);
                systemLog.info("Auto-arm: no presence " + String(autoArmMin) + "min");
                eventLog.addEvent(EVT_SECURITY, 0, 0, "Auto-arm (no presence)");
                if (telegramBot.isEnabled()) {
                    telegramBot.sendMessage("🔒 Auto-arm: no movement " + String(autoArmMin) + " min");
                }
            }
        }
    }

    RadarData data = radar.getData();

    // LED heartbeat (Stealth Mode)
    bool isArmedActive = securityMonitor.isArmed();
    bool securityAlert = data.tamper_alert
        || (isArmedActive && securityMonitor.isBlind() && securityMonitor.isAntiMaskEnabled())
        || (isArmedActive && securityMonitor.isLoitering() && securityMonitor.isLoiterAlertEnabled())
        || (securityMonitor.getAlarmState() == AlarmState::TRIGGERED);

    // Offline Alarm Memory
    static bool offlineAlarmOccurred = false;
    static unsigned long offlineAlarmTime = 0;

    if (securityAlert) {
        if (configManager.getConfig().led_enabled && now - lastLedBlink > 100) {
            lastLedBlink = now;
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        }
        if (!mqttService.connected() && !offlineAlarmOccurred) {
            offlineAlarmOccurred = true;
            offlineAlarmTime = now;
            systemLog.error("OFFLINE ALARM DETECTED! Waiting for sync...");
        }
    } else if (configManager.getConfig().led_enabled && (now - bootTime) < (unsigned long)configManager.getConfig().startup_led_sec * 1000) {
        if (now - lastLedBlink > 1000) {
            lastLedBlink = now;
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        }
    } else {
        digitalWrite(LED_PIN, LOW);
    }

    // Sync Offline Alarm once connected
    if (mqttService.connected() && offlineAlarmOccurred) {
        unsigned long diff = (now - offlineAlarmTime) / 1000;
        String msg = "⚠️ SYNC: Alarm occurred during network outage! ( " + String(diff) + "s)";
        notificationService.sendTelegram(msg);
        mqttService.publish("home/security/log", msg.c_str());
        offlineAlarmOccurred = false;
    }

    // Static learn completion notification
    if (radar.consumeLearnDone()) {
        JsonDocument learnDoc;
        radar.getLearnResultJson(learnDoc);
        String learnMsg = "📡 *Static Learn completed*\n";
        learnMsg += "Samples: " + String(learnDoc["static_samples"].as<int>()) + " / " + String(learnDoc["total_samples"].as<int>()) + "\n";
        learnMsg += "Static: " + String(learnDoc["static_freq_pct"].as<int>()) + "%\n";
        int topGate = learnDoc["top_gate"] | 0;
        learnMsg += "Top gate: " + String(topGate) + " (~" + String(topGate * 75) + "cm)\n";
        learnMsg += "Confidence: " + String(learnDoc["confidence"].as<int>()) + "%\n";
        if (learnDoc["suggest_ready"] | false) {
            learnMsg += "✅ Suggested zone: " + String(learnDoc["suggest_min_cm"].as<int>()) + "–" + String(learnDoc["suggest_max_cm"].as<int>()) + "cm (ignore\\_static\\_only)";
        } else {
            learnMsg += "⚠️ Not enough data for zone suggestion.";
        }
        notificationService.sendTelegram(learnMsg);
    }

    // Security alarm trigger — 50ms tick (20 Hz) to catch short detections
    // FIX #9: Skip evaluation when radar data is unavailable (mutex timeout)
    static unsigned long lastAlarmCheck = 0;
    if (now - lastAlarmCheck >= 50 && data.valid) {
        lastAlarmCheck = now;
        securityMonitor.processRadarData(data.distance_cm, data.moving_energy, data.static_energy);
        radar.setTamperDetected(securityMonitor.isBlind() && securityMonitor.isAntiMaskEnabled());
    }

    // Slow diagnostics — 1s tick
    static unsigned long lastSecCheck = 0;
    if (now - lastSecCheck >= 1000) {
        lastSecCheck = now;
        securityMonitor.checkTamperState(data.tamper_alert);
        securityMonitor.checkRadarHealth(radar.isRadarConnected());
        // RSSI anomaly detection removed — Ethernet doesn't have RSSI
    }

    // OTA Rollback Validation
    if (!bootValidated && (now - bootTime) > TIMEOUT_OTA_VALIDATION_MS) {
        const esp_partition_t *running = esp_ota_get_running_partition();
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
            if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
                esp_ota_mark_app_valid_cancel_rollback();
                DBG("OTA", "App verified & rollback cancelled!");
                systemLog.info("OTA Update verified OK");
            }
        }
        bootValidated = true;
    }

    // Update last_uptime periodically
    static unsigned long lastUptimeSave = 0;
    if (now - lastUptimeSave > INTERVAL_UPTIME_SAVE_MS) {
        lastUptimeSave = now;
        preferences.putULong("last_uptime", now / 1000);
    }

    // MQTT soft-recovery: if no successful publish for TIMEOUT_MQTT_SOFT_RECOVERY_MS,
    // force a clean reconnect well before DMS (30 min) would intervene. Rate-limited
    // to once per window to avoid reconnect storms during broker outages.
    static unsigned long softRecoveryLast = 0;
    if (configManager.getConfig().mqtt_enabled && strlen(configManager.getConfig().mqtt_server) > 0 &&
        (now - bootTime) > TIMEOUT_DMS_STARTUP_MS) {
        unsigned long softAge = (unsigned long)(now - mqttService.getLastPublishTime());
        bool softStale = (softAge > TIMEOUT_MQTT_SOFT_RECOVERY_MS) && (softAge < 2592000000UL);
        if (softStale && (now - softRecoveryLast) > TIMEOUT_MQTT_SOFT_RECOVERY_MS) {
            DBG("MQTT", "Soft-recovery: publish stale %lus — force reconnect", softAge / 1000);
            systemLog.warn("MQTT soft-recovery (publish stale " + String(softAge / 1000) + "s)");
            mqttService.forceReconnect();
            softRecoveryLast = now;
        }
    }

    // Dead Man's Switch — with MQTT reconnect before restart
    static uint8_t dmsRestarts = preferences.getUInt("dms_count", 0);
    static bool dmsDegraded = false;
    static bool dmsReconnectPending = false;
    static unsigned long dmsReconnectTime = 0;
    unsigned long dmsPublishAge = (unsigned long)(now - mqttService.getLastPublishTime());
    // Guard against millis() overflow: if age > 30 days, it's clearly wrap-around, not real staleness
    bool dmsPublishStale = (dmsPublishAge > TIMEOUT_DMS_NO_PUBLISH_MS) && (dmsPublishAge < 2592000000UL);
    // Skip DMS entirely while an OTA is mid-flight (hypothesis #4 from
    // docs/OTA_FAILURE_2026_05_02.md: 30-min DMS firing mid-upload returns
    // Update.hasError()=true on resume) or while the operator has explicitly
    // pinned the device for stability testing via reboot inhibit.
#ifdef USE_CSI
    bool dmsBlocked = CSIService::isOtaInProgress() || g_rebootInhibit.load();
#else
    bool dmsBlocked = g_rebootInhibit.load();
#endif
    if (!dmsDegraded && !dmsBlocked && configManager.getConfig().mqtt_enabled && strlen(configManager.getConfig().mqtt_server) > 0 && (now - bootTime) > TIMEOUT_DMS_STARTUP_MS &&
        dmsPublishStale) {

        if (!dmsReconnectPending) {
            // Phase 1: Try MQTT reconnect first before restarting ESP
            unsigned long publishAge = dmsPublishAge / 1000;
            DBG("SYSTEM", "DMS: No publish for %lus, connected=%d — forcing MQTT reconnect",
                publishAge, mqttService.connected());
            systemLog.error("DMS: publish stale " + String(publishAge) + "s — reconnecting MQTT");
            mqttService.forceReconnect();
            dmsReconnectPending = true;
            dmsReconnectTime = now;
        } else if ((now - dmsReconnectTime) > 60000) {
            // Phase 2: 60s after reconnect — check if publish recovered
            unsigned long phase2Age = (unsigned long)(now - mqttService.getLastPublishTime());
            if (phase2Age > TIMEOUT_DMS_NO_PUBLISH_MS && phase2Age < 2592000000UL) {
                if (dmsRestarts < DMS_MAX_RESTARTS) {
                    dmsRestarts++;
                    preferences.putUInt("dms_count", dmsRestarts);
                    DBG("SYSTEM", "DMS: Reconnect failed. Restart %d/%d", dmsRestarts, DMS_MAX_RESTARTS);
                    systemLog.error("DMS restart #" + String(dmsRestarts) + " (reconnect failed)");
                    safeRestart("dms_no_mqtt_publish");
                } else {
                    dmsDegraded = true;
                    DBG("SYSTEM", "DMS: Max restarts reached. Degraded mode (local only).");
                    systemLog.error("DMS: Degraded mode — MQTT offline, local operation only");
                }
            } else {
                DBG("SYSTEM", "DMS: MQTT reconnect resolved publish issue");
                dmsReconnectPending = false;
            }
        }
    } else if (dmsReconnectPending && !dmsPublishStale) {
        dmsReconnectPending = false;
    }
    // Reset DMS counter after successful publish — read-before-write so a
    // RAM-only reset doesn't issue a redundant flash erase on the dms_count
    // NVS page.
    if (dmsRestarts > 0 && mqttService.getLastPublishTime() > 0 && !dmsPublishStale) {
        if (preferences.getUInt("dms_count", 0) != 0) {
            preferences.putUInt("dms_count", 0);
        }
        dmsRestarts = 0;
        if (dmsDegraded) {
            dmsDegraded = false;
            DBG("SYSTEM", "MQTT publish restored — exiting degraded mode");
            systemLog.info("MQTT restored, DMS counter reset");
        }
    }

    // SSE Realtime Telemetry (250ms)
#ifndef LITE_BUILD
    static unsigned long lastSSE = 0;
    // rc7-fix1c: skip SSE JSON serialization during OTA. Per Petr 2026-05-02
    // ("nezatěžovat ESP během flashe"): JsonDocument allocation + events->send
    // every 250 ms is wasted heap pressure when AsyncTCP needs every byte
    // for the upload pbuf chain.
#ifdef USE_CSI
    bool sseSkipForOta = CSIService::isOtaInProgress();
#else
    bool sseSkipForOta = false;
#endif
    if (!sseSkipForOta && now - lastSSE > INTERVAL_SSE_UPDATE_MS && ESP.getFreeHeap() >= HEAP_MIN_FOR_PUBLISH) {
        lastSSE = now;
        JsonDocument doc;
        radar.getTelemetryJson(doc);
        doc["uptime"] = now / 1000;
        doc["eth_link"] = ETH.linkUp();
        doc["armed"] = securityMonitor.isArmed();
        doc["alarm_state"] = securityMonitor.getAlarmStateStr();
        #if RADAR_OUT_PIN >= 0
        doc["out_pin"] = digitalRead(RADAR_OUT_PIN);
        #endif

        // CSI live telemetry (only when compiled in AND runtime-enabled)
        #ifdef USE_CSI
        if (configManager.getConfig().csi_enabled && csiService.isActive()) {
            JsonObject csi = doc["csi"].to<JsonObject>();
            csi["motion"]    = csiService.getMotionState();
            csi["composite"] = csiService.getCompositeScore();
            csi["variance"]  = csiService.getVariance();
            csi["dser"]      = csiService.getDser();
            csi["plcr"]      = csiService.getPlcr();
            csi["pps"]       = csiService.getPacketRate();
            csi["packets"]   = csiService.getPacketCount();
            csi["rssi"]      = csiService.getWifiRSSI();
            csi["calibrating"] = csiService.isCalibrating();
            csi["calib_pct"] = csiService.getCalibrationProgress();

            // csi9: live ML + site learning for GUI tab 6
            csi["ml_enabled"]     = csiService.isMlEnabled();
            csi["ml_motion"]      = csiService.getMlMotionState();
            csi["ml_probability"] = csiService.getMlProbability();
            csi["learning_active"]   = csiService.isSiteLearning();
            if (csiService.isSiteLearning()) {
                csi["learning_progress"]   = csiService.getSiteLearningProgress() * 100.0f;
                csi["learning_elapsed_s"]  = csiService.getSiteLearningElapsedSec();
                csi["learning_duration_s"] = csiService.getSiteLearningDurationSec();
                csi["learning_samples"]    = csiService.getSiteLearningAcceptedSamples();
            }

            // Fusion state in SSE telemetry
            if (securityMonitor.isFusionActive()) {
                JsonObject fusion = doc["fusion"].to<JsonObject>();
                fusion["presence"]   = securityMonitor.isFusionPresence();
                fusion["confidence"] = securityMonitor.getFusionConfidence();
                fusion["source"]     = securityMonitor.getFusionSourceStr();
            }
        }
        #endif

        // Buffer must fit full telemetry JSON (~700-900 B incl. eng_mode arrays,
        // +~120 B pri zapnutem CSI). v4.1.4: zvyseno z 1024 -> 1536 kvuli CSI poli.
        // Predchozi 256 B (pred v4.1.3) silently dropped every event since
        // serializeJson returns sizeof(buf) on overflow -> guard byl false.
        char sseBuf[1536];
        size_t sseLen = serializeJson(doc, sseBuf, sizeof(sseBuf));
        if (sseLen > 0 && sseLen < sizeof(sseBuf)) {
            events.send(sseBuf, "telemetry", millis());
        }
    }
#endif

    // =====================================================================
    // MQTT Publish-on-Change with Deadband (3-tier system)
    // =====================================================================

    if (ESP.getFreeHeap() < HEAP_MIN_FOR_PUBLISH) {
        static unsigned long lastHeapWarn = 0;
        if (now - lastHeapWarn > 10000) { lastHeapWarn = now; Serial.println("[WARN] Low heap — skipping MQTT publish"); }
    }
    else if (configManager.getConfig().mqtt_enabled) {
        const MQTTTopics& topics = mqttService.getTopics();

        // --- TIER 1: Critical state changes ---
        {
            const char* stateStr = "idle";
            if (data.state == PresenceState::PRESENCE_DETECTED) stateStr = "detected";
            else if (data.state == PresenceState::HOLD_TIMEOUT) stateStr = "detected";
            else if (data.state == PresenceState::TAMPER) stateStr = "detected";

            // FIX #10: Only update lastPub cache after successful publish
            if (strcmp(stateStr, lastPub.presence_state) != 0) {
                if (mqttService.publish(topics.presence_state, stateStr, true)) {
                    strncpy(lastPub.presence_state, stateStr, sizeof(lastPub.presence_state) - 1);
                    lastPub.presence_state[sizeof(lastPub.presence_state) - 1] = '\0';
                }
            }
            if (data.tamper_alert != lastPub.tamper) {
                if (mqttService.publish(topics.tamper, data.tamper_alert ? "true" : "false", true))
                    lastPub.tamper = data.tamper_alert;
            }
            bool blind = securityMonitor.isBlind();
            if (blind != lastPub.anti_masking) {
                if (mqttService.publish(topics.alert_anti_masking, blind ? "true" : "false", true))
                    lastPub.anti_masking = blind;
            }
            bool loiter = securityMonitor.isLoitering();
            if (loiter != lastPub.loitering) {
                if (mqttService.publish(topics.alert_loitering, loiter ? "true" : "false", true))
                    lastPub.loitering = loiter;
            }
            const char* alarmStr = securityMonitor.getAlarmStateStr();
            if (strcmp(alarmStr, lastPub.alarm_state) != 0) {
                if (mqttService.publish(topics.alarm_state, alarmStr, true)) {
                    strncpy(lastPub.alarm_state, alarmStr, sizeof(lastPub.alarm_state) - 1);
                    lastPub.alarm_state[sizeof(lastPub.alarm_state) - 1] = '\0';
                }
            }

            // Fusion presence (on-change)
            if (securityMonitor.isFusionActive()) {
                bool fusionPres = securityMonitor.isFusionPresence();
                if (fusionPres != lastPub.fusion_presence) {
                    if (mqttService.publish(topics.fusion_presence, fusionPres ? "ON" : "OFF", true))
                        lastPub.fusion_presence = fusionPres;
                }
                const char* fusionSrc = securityMonitor.getFusionSourceStr();
                if (strcmp(fusionSrc, lastPub.fusion_source) != 0) {
                    if (mqttService.publish(topics.fusion_source, fusionSrc, true)) {
                        strncpy(lastPub.fusion_source, fusionSrc, sizeof(lastPub.fusion_source) - 1);
                        lastPub.fusion_source[sizeof(lastPub.fusion_source) - 1] = '\0';
                    }
                }
                // Confidence as float string (deadband 0.05)
                float fusionConf = securityMonitor.getFusionConfidence();
                if (fabsf(fusionConf - lastPub.fusion_confidence) > 0.05f) {
                    char confBuf[8];
                    snprintf(confBuf, sizeof(confBuf), "%.2f", fusionConf);
                    if (mqttService.publish(topics.fusion_confidence, confBuf, true))
                        lastPub.fusion_confidence = fusionConf;
                }
            }

            // motion_type: "moving" | "static" | "both" | "none"
            const char* mtStr = "none";
            if (!securityMonitor.isStaticFiltered() && data.state != PresenceState::IDLE) {
                if (data.moving_energy > 0 && data.static_energy > 0) mtStr = "both";
                else if (data.moving_energy > 0) mtStr = "moving";
                else if (data.static_energy > 0) mtStr = "static";
            }
            if (strcmp(mtStr, lastPub.motion_type) != 0) {
                if (mqttService.publish(topics.motion_type, mtStr, true)) {
                    strncpy(lastPub.motion_type, mtStr, sizeof(lastPub.motion_type) - 1);
                    lastPub.motion_type[sizeof(lastPub.motion_type) - 1] = '\0';
                }
            }

            // alarm/event: atomic JSON on PENDING/TRIGGERED
            // FIX #5: Peek first, only consume after successful publish
            {
                AlarmTriggerEvent evt;
                while (securityMonitor.peekAlarmEvent(evt)) {
                    JsonDocument evtDoc;
                    evtDoc["reason"]      = evt.reason;
                    evtDoc["zone"]        = evt.zone;
                    evtDoc["distance_cm"] = evt.distance_cm;
                    evtDoc["energy_mov"]  = evt.energy_mov;
                    evtDoc["energy_stat"] = evt.energy_stat;
                    evtDoc["motion_type"] = evt.motion_type;
                    evtDoc["uptime_s"]    = evt.uptime_s;
                    if (evt.iso_time[0]) evtDoc["time"] = evt.iso_time;

                    // rc2: forensic fields
                    evtDoc["trigger_source"]    = evt.trigger_source;
                    evtDoc["fusion_confidence"] = evt.fusion_confidence;
                    evtDoc["static_filtered"]   = (bool)evt.static_filtered;
                    evtDoc["zone_was_none"]     = (bool)evt.zone_was_none;
                    evtDoc["prev_zone"]         = evt.prev_zone;

                    // rc2: pre-trigger ring buffer (oldest → newest, age_ms = ms before trigger)
                    if (evt.ring_count > 0) {
                        JsonArray ring = evtDoc["pre_trigger"].to<JsonArray>();
                        for (uint8_t i = 0; i < evt.ring_count; i++) {
                            JsonObject s = ring.add<JsonObject>();
                            s["age_ms"] = evt.ring[i].age_ms;
                            s["d"]      = evt.ring[i].distance_cm;
                            s["mov"]    = evt.ring[i].move_energy;
                            s["stat"]   = evt.ring[i].static_energy;
                            s["src"]    = evt.ring[i].fusion_source;
                            s["fl"]     = evt.ring[i].flags;
                        }
                    }

                    // Mesh: include verification status
                    if (peerCount > 0) {
                        evtDoc["mesh_peers"]     = peerCount;
                        evtDoc["mesh_confirmed"] = meshConfirmCount;
                        evtDoc["mesh_verified"]  = (meshConfirmCount > 0);
                    }

                    String evtJson;
                    serializeJson(evtDoc, evtJson);
                    if (mqttService.publish(topics.alarm_event, evtJson.c_str(), false)) {
                        securityMonitor.consumeAlarmEvent();
                    } else {
                        break; // Retry next loop iteration
                    }

                    // Mesh: send verify request to peers on entry_delay or immediate events
                    if (peerCount > 0 && (strcmp(evt.reason, "entry_delay") == 0 || strcmp(evt.reason, "immediate") == 0)) {
                        meshVerifyPending = true;
                        meshVerifyRequestTime = now;
                        meshConfirmCount = 0;
                        mqttService.publish(topics.mesh_verify_request, evtJson.c_str(), false);
                        DBG("MESH", "Verify request sent to %d peers", peerCount);
                    }
                }
            }

            // Mesh: check verify timeout — log result
            if (meshVerifyPending && now - meshVerifyRequestTime > MESH_VERIFY_TIMEOUT_MS) {
                meshVerifyPending = false;
                if (meshConfirmCount > 0) {
                    DBG("MESH", "Alarm VERIFIED by %d peer(s)", meshConfirmCount);
                } else {
                    DBG("MESH", "Alarm UNVERIFIED (no peers confirmed within %lus)", MESH_VERIFY_TIMEOUT_MS / 1000);
                }
            }
        }

        // --- TIER 2: Primary sensor data ---
        {
            unsigned long teleInterval = INTERVAL_TELEMETRY_IDLE_MS;
            if (data.state != PresenceState::IDLE || data.tamper_alert || securityMonitor.isBlind() || securityMonitor.isLoitering()) {
                teleInterval = INTERVAL_TELEMETRY_ACTIVE_MS;
            }

            if (now - lastTele > teleInterval) {
                lastTele = now;

                char numBuf[16];
                if (changedU16(data.distance_cm, lastPub.distance_cm, DEADBAND_DISTANCE_CM)) {
                    snprintf(numBuf, sizeof(numBuf), "%u", data.distance_cm);
                    if (mqttService.publish(topics.distance, numBuf)) {
                        lastPub.distance_cm = data.distance_cm;
                    }
                }
                if (changedU8(data.moving_energy, lastPub.energy_mov, DEADBAND_ENERGY)) {
                    snprintf(numBuf, sizeof(numBuf), "%u", data.moving_energy);
                    if (mqttService.publish(topics.energy_mov, numBuf)) {
                        lastPub.energy_mov = data.moving_energy;
                    }
                }
                if (changedU8(data.static_energy, lastPub.energy_stat, DEADBAND_ENERGY)) {
                    snprintf(numBuf, sizeof(numBuf), "%u", data.static_energy);
                    if (mqttService.publish(topics.energy_stat, numBuf)) {
                        lastPub.energy_stat = data.static_energy;
                    }
                }
                String dirNow = securityMonitor.getDirection();
                if (strcmp(dirNow.c_str(), lastPub.direction) != 0) {
                    if (mqttService.publish(topics.motion_direction, dirNow.c_str())) {
                        strncpy(lastPub.direction, dirNow.c_str(), sizeof(lastPub.direction) - 1);
                        lastPub.direction[sizeof(lastPub.direction) - 1] = '\0';
                    }
                }
            }
        }

        // --- TIER 3: Diagnostics (30s + deadband) ---
        if (now - lastPub.lastDiagPublish > INTERVAL_TELEMETRY_DIAG_MS) {
            lastPub.lastDiagPublish = now;

            // Heartbeat: every 5 min invalidate deadband-gated cache so HA
            // receives periodic refresh of unchanged diagnostics (health_score,
            // uart_state, frame_rate, heap). Addresses Codex 23-min follow-up
            // finding: HA last_reported froze on stable values while ESP was healthy.
            static unsigned long lastTier3Heartbeat = 0;
            if (now - lastTier3Heartbeat >= INTERVAL_TIER3_HEARTBEAT_MS) {
                lastTier3Heartbeat = now;
                lastPub.health_score = 0xFF;
                lastPub.frame_rate   = -1.0f;
                lastPub.error_count  = 0xFFFFFFFFUL;
                memset(lastPub.uart_state, 0, sizeof(lastPub.uart_state));
                lastPub.free_heap_kb = 0;
                lastPub.max_alloc_kb = 0;
                // Dedicated heartbeat topic with unique-per-tick uptime —
                // HA cannot dedup this because value monotonically grows.
                char hbBuf[16];
                snprintf(hbBuf, sizeof(hbBuf), "%lu", now / 1000);
                mqttService.publish(topics.heartbeat, hbBuf, false);
                DBG("MQTT", "TIER 3 heartbeat — forcing diag republish + heartbeat topic");
            }

            // Uptime always published (DMS keepalive)
            char numBuf[16];
            uint32_t curUptime = now / 1000;
            snprintf(numBuf, sizeof(numBuf), "%u", curUptime);
            mqttService.publish(topics.uptime, numBuf);
            lastPub.uptime_s = curUptime;

            // ETH link status (replaces WiFi RSSI)
            mqttService.publish(topics.rssi, ETH.linkUp() ? "ON" : "OFF", true);

            uint8_t curHealth = radar.getHealthScore();
            if (changedU8(curHealth, lastPub.health_score, DEADBAND_HEALTH_SCORE)) {
                snprintf(numBuf, sizeof(numBuf), "%u", curHealth);
                if (mqttService.publish(topics.health_score, numBuf)) {
                    lastPub.health_score = curHealth;
                }
            }

            float curFR = radar.getFrameRate();
            if (changedF(curFR, lastPub.frame_rate, DEADBAND_FRAME_RATE)) {
                snprintf(numBuf, sizeof(numBuf), "%.1f", curFR);
                if (mqttService.publish(topics.frame_rate, numBuf)) {
                    lastPub.frame_rate = curFR;
                }
            }

            uint32_t curErrors = radar.getErrorCount();
            if (curErrors != lastPub.error_count) {
                snprintf(numBuf, sizeof(numBuf), "%u", curErrors);
                if (mqttService.publish(topics.error_count, numBuf)) {
                    lastPub.error_count = curErrors;
                }
            }

            const char* curUart = radar.getUARTStateString();
            if (strcmp(curUart, lastPub.uart_state) != 0) {
                if (mqttService.publish(topics.uart_state, curUart)) {
                    strncpy(lastPub.uart_state, curUart, sizeof(lastPub.uart_state) - 1);
                    lastPub.uart_state[sizeof(lastPub.uart_state) - 1] = '\0';
                }
            }

            uint32_t curHeap = ESP.getFreeHeap() / 1024;
            if (changedU32(curHeap, lastPub.free_heap_kb, DEADBAND_FREE_HEAP_KB)) {
                snprintf(numBuf, sizeof(numBuf), "%u", curHeap);
                if (mqttService.publish(topics.free_heap, numBuf)) {
                    lastPub.free_heap_kb = curHeap;
                }
            }

            uint32_t curMaxAlloc = ESP.getMaxAllocHeap() / 1024;
            if (changedU32(curMaxAlloc, lastPub.max_alloc_kb, DEADBAND_FREE_HEAP_KB)) {
                snprintf(numBuf, sizeof(numBuf), "%u", curMaxAlloc);
                if (mqttService.publish(topics.max_alloc_heap, numBuf)) {
                    lastPub.max_alloc_kb = curMaxAlloc;
                }
            }

            bool curEngMode = radar.isEngineeringMode();
            if (curEngMode != lastPub.eng_mode) {
                if (mqttService.publish(topics.eng_mode, curEngMode ? "true" : "false", true)) {
                    lastPub.eng_mode = curEngMode;
                }
            }

            // Heap Telegram alerts
            if (telegramBot.isEnabled()) {
                uint32_t freeHeap = ESP.getFreeHeap();
                bool heapCooldownOk = (now - lastPub.lastHeapAlert) > COOLDOWN_HEAP_ALERT_MS;
                if (freeHeap <= HEAP_CRIT_BYTES && heapCooldownOk) {
                    telegramBot.sendMessage("🔴 *CRITICALLY LOW RAM*\n💾 Free: " + String(freeHeap / 1024) + " KB (limit " + String(HEAP_CRIT_BYTES / 1024) + " KB)\nCrash risk!");
                    lastPub.lastHeapAlert = now;
                    lastPub.heapAlertActive = true;
                } else if (freeHeap <= HEAP_WARN_BYTES && heapCooldownOk) {
                    telegramBot.sendMessage("⚠️ *Low RAM*\n💾 Free: " + String(freeHeap / 1024) + " KB (limit " + String(HEAP_WARN_BYTES / 1024) + " KB)");
                    lastPub.lastHeapAlert = now;
                    lastPub.heapAlertActive = true;
                } else if (lastPub.heapAlertActive && freeHeap >= HEAP_RECOVER_BYTES) {
                    telegramBot.sendMessage("✅ RAM normal\n💾 Free: " + String(freeHeap / 1024) + " KB");
                    lastPub.heapAlertActive = false;
                }
            }
        }

        // --- TIER TEMP: Chip temperature (configurable interval + 5°C delta + Telegram alerts) ---
        {
            float curTemp = temperatureRead();

            // MQTT publish
            uint16_t tempIntv = configManager.getConfig().chip_temp_interval;
            if (tempIntv > 0) {
                unsigned long tempIntervalMs = (unsigned long)tempIntv * 1000UL;
                bool tempTimeout = (now - lastPub.lastTempPublish) >= tempIntervalMs;
                bool tempDelta   = changedF(curTemp, lastPub.chip_temp, 5.0f);
                if (tempTimeout || (tempDelta && lastPub.chip_temp > -90.0f)) {
                    const MQTTTopics& topics = mqttService.getTopics();
                    if (mqttService.publish(topics.chip_temp, String(curTemp, 1).c_str())) {
                        lastPub.chip_temp = curTemp;
                        lastPub.lastTempPublish = now;
                    }
                }
            }

            // Telegram alerts (cooldown 30 min)
            if (telegramBot.isEnabled()) {
                bool cooldownOk = (now - lastPub.lastTempAlert) > COOLDOWN_CHIP_TEMP_ALERT_MS;
                if (curTemp >= CHIP_TEMP_CRIT_C && cooldownOk) {
                    telegramBot.sendMessage("🔥 *CRITICAL CHIP TEMPERATURE*\n🌡️ " + String(curTemp, 1) + " °C (limit " + String((int)CHIP_TEMP_CRIT_C) + "°C)\nDamage risk!");
                    lastPub.lastTempAlert = now;
                    lastPub.tempAlertActive = true;
                } else if (curTemp >= CHIP_TEMP_WARN_C && cooldownOk) {
                    telegramBot.sendMessage("⚠️ *High chip temperature*\n🌡️ " + String(curTemp, 1) + " °C (limit " + String((int)CHIP_TEMP_WARN_C) + "°C)");
                    lastPub.lastTempAlert = now;
                    lastPub.tempAlertActive = true;
                } else if (lastPub.tempAlertActive && curTemp <= CHIP_TEMP_RECOVER_C) {
                    telegramBot.sendMessage("✅ Chip temperature normal\n🌡️ " + String(curTemp, 1) + " °C");
                    lastPub.tempAlertActive = false;
                }
            }
        }

        // --- TIER ENG: Engineering gate data (10s + deadband) ---
        if (radar.isEngineeringMode() && (now - lastPub.lastEngPublish > INTERVAL_TELEMETRY_ENG_MS)) {
            lastPub.lastEngPublish = now;

            uint8_t curLight = radar.getLightLevel();
            if (changedU8(curLight, lastPub.light_level, DEADBAND_GATE_ENERGY)) {
                if (mqttService.publish(topics.light, String(curLight).c_str())) {
                    lastPub.light_level = curLight;
                }
            }

            uint8_t movCopy[14], statCopy[14];
            radar.getGateEnergiesSafe(movCopy, statCopy);

            for (int i = 0; i < 14; i++) {
                if (changedU8(movCopy[i], lastPub.gate_mov[i], DEADBAND_GATE_ENERGY)) {
                    char tMov[96];
                    snprintf(tMov, sizeof(tMov), "%s%d/moving", topics.eng_gate_base, i);
                    if (mqttService.publish(tMov, String(movCopy[i]).c_str())) {
                        lastPub.gate_mov[i] = movCopy[i];
                    }
                }
                if (changedU8(statCopy[i], lastPub.gate_stat[i], DEADBAND_GATE_ENERGY)) {
                    char tStat[96];
                    snprintf(tStat, sizeof(tStat), "%s%d/static", topics.eng_gate_base, i);
                    if (mqttService.publish(tStat, String(statCopy[i]).c_str())) {
                        lastPub.gate_stat[i] = statCopy[i];
                    }
                }
            }
        }

        // --- Supervision heartbeat: publish alive + check peers ---
        if (now - lastSupervisionPublish > SUPERVISION_INTERVAL_MS) {
            lastSupervisionPublish = now;
            mqttService.publish(topics.supervision_alive, "1", false);
            supervisionCheck();
        }
    } // end mqtt_enabled

    // --- STABILITY LOGGER (5s interval) ---
    static unsigned long lastStabilityLog = 0;
    if (now - lastStabilityLog > 5000) {
        lastStabilityLog = now;
        Serial.printf("STAB|%lu|%d|%d|%d|ETH:%s|%u|%u|%u|%s|MQTT:%s\n",
            now / 1000,
            data.distance_cm,
            data.moving_energy,
            data.static_energy,
            ETH.linkUp() ? "UP" : "DOWN",
            ESP.getFreeHeap(),
            ESP.getMinFreeHeap(),
            ESP.getMaxAllocHeap(),
            securityMonitor.getAlarmStateStr(),
            mqttService.connected() ? "CONNECTED" : "DISCONNECTED"
        );
    }

    delay(10);
}
