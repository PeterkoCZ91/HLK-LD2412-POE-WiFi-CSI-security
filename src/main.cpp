#include <Arduino.h>
#include <ETH.h>
#include "services/HeapMetrics.h"
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
#include "services/AuthLockout.h"
#include "services/MqttCommandRouter.h"
#include "services/NotificationService.h"
#include "services/TelegramService.h"
#include "services/LogService.h"
#include "services/HeapWatermarkTripwire.h"
#include "services/HeapSkipTracker.h"
#include "services/HeapWatermarkRing.h"
#include "services/DmsPolicy.h"
#include "services/EventLog.h"
#include "services/MlFeedbackStore.h"
#include "services/RuntimeOperationCoordinator.h"
#include "services/ConfigSnapshot.h"
#include "services/MQTTOfflineBuffer.h"
#include "services/HeapGatePolicy.h"   // dev7 L1: web low-heap accept gate
#include "services/EthLinkFlapTracker.h" // real PHY flap accounting (the watchdog only aliases it)
#include "services/OomGuard.h"         // dev7 L3: OOM new-handler marker + restart decision
#include "services/BootOutageNotice.h" // dev8: dirty-boot Telegram notice
#include <esp_core_dump.h>
#ifndef LITE_BUILD
#include "services/GatedWebServer.h"   // dev7 L1: AsyncWebServer with gated accept
#endif
#include <new>                          // std::set_new_handler
#ifndef NO_BLUETOOTH
#include "services/BluetoothService.h"
#endif
#ifdef USE_CSI
#include "services/CSIService.h"
#include "services/FusionReason.h"   // #8 fusion panel — reason string
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
#include "FirmwareVersion.h"
#include "services/HeapActivity.h"
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
// dev7 L1: accept gate shared by the web server (async_tcp), the SSE connect
// handler, /api/health and the loop-task transition logging.
HeapGatePolicy g_webHeapGate;
// Counts the driver's own link edges. The 60 s connectivity watchdog below can
// only ever see a 2 s glitch as "60 s down"; this sees what actually happened.
EthLinkFlapTracker g_ethFlap;
#ifndef LITE_BUILD
GatedAsyncWebServer server(80, g_webHeapGate);
AsyncEventSource events("/events");
#endif
Preferences preferences;
MQTTService mqttService;
SecurityMonitor securityMonitor;
// Brute-force lockout for the MQTT alarm PIN. MQTT has no per-client IP, so a
// single shared bucket (key 0) throttles all attempts globally (S-0b).
AuthLockout mqttCmdLockout;
#ifdef USE_CSI
CSIService csiService;
#endif
NotificationService notificationService;
TelegramService telegramBot;
LogService systemLog(20);
// dev19: the publish guard fires while STAB reports 59 kB free five seconds
// either side, so the dip lives between two samples. Count every skip, log a
// few — see include/services/HeapSkipTracker.h.
HeapSkipTracker g_mqttHeapSkips(10000);
HeapSkipTracker g_sseHeapSkips(10000);

// dev17 heap-spike forensics — see include/services/HeapWatermarkTripwire.h.
// 2 kB minimum drop, at most 8 log lines for the whole boot: LogRtcRing has
// 20 slots shared with ordinary logging and must not be flooded by its own
// instrument.
HeapWatermarkTripwire g_heapWatermarkTripwire(2048, 8);
// Log ring v RTC noinit RAM — přežije panic i SW reset (ne power-cycle),
// takže /api/logs po pádu ukáže i záznamy z doby před restartem.
RTC_NOINIT_ATTR static LogRtcRing g_rtcLog;
// dev18: the tripwire's OWN ring. It used to write through systemLog into
// g_rtcLog above — which the ETH link handler turns over in ~50 min on a
// flapping link, evicting the very evidence the tripwire collects. Separate
// storage, nobody else writes here.
RTC_NOINIT_ATTR HeapWatermarkRtcRing g_heapWmRing;
HeapWatermarkRtcRing g_heapWmPrevBoot;   // RAM copy carried from the last boot
bool g_heapWmPrevBootValid = false;
// dev7 L3: OOM incident marker — the new-handler must not allocate, so the
// record survives in RTC noinit RAM and is folded into reset_history at boot.
RTC_NOINIT_ATTR static OomMarker g_oomMarker;
static bool g_lastBootWasOomRestart = false;   // loop guard input, set once at boot
static std::atomic<bool> g_oomRestartEnabled{true};   // NVS oom_restart_en
EventLog eventLog(RAM_CAPACITY);
MlFeedbackStore mlFeedbackStore;
ConfigSnapshot configSnapshot;
MQTTOfflineBuffer mqttOfflineBuffer;
#ifndef NO_BLUETOOTH
BluetoothService btService;
#endif
TaskHandle_t radarTaskHandle = nullptr;
String g_prevRestartCause = "none";
// dev8: boot outage notices — built in setup() from the reset-history data,
// delivered by loop one-shots once the network is up, then cleared. The text
// goes to the node's own Telegram (if configured); the JSON goes NON-retained
// to security/<id>/system/outage for an HA automation to forward (HA-side
// Telegram is the deployment norm — the node-side bot is often unconfigured).
static String g_bootOutageNotice;
static String g_bootOutageEventJson;

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
// CSI data health: true when WiFi is associated but no CSI frames arrive (weak
// signal / AP issue) → detection is starved. Surfaced in /api/health + metrics.
std::atomic<bool> g_csiDataStarved{false};
// dev13: SSE ticks dropped because a client had not drained its queue.
std::atomic<uint32_t> g_sseBacklogSkips{0};
RuntimeOperationCoordinator g_runtimeOperationCoordinator;
#ifndef LITE_BUILD
std::atomic<bool> g_espotaPrepareRequested{false};
std::atomic<bool> g_espotaMaintenance{false};
std::atomic<uint32_t> g_espotaMaintenanceSeconds{120};
std::atomic<unsigned long> g_espotaMaintenanceUntilMs{0};
std::atomic<uint32_t> g_otaRuntimeLastBytes{0};
static constexpr uint8_t OTA_OWNER_NONE = 0;
static constexpr uint8_t OTA_OWNER_HTTP = 1;
static constexpr uint8_t OTA_OWNER_PULL = 2;
static constexpr uint8_t OTA_OWNER_ESPOTA_PREPARE = 3;
static constexpr uint8_t OTA_OWNER_ESPOTA = 4;
const char* otaRuntimeOwnerName(uint8_t owner);
uint8_t otaRuntimeOwner();
bool otaRuntimeTransferActive();
uint32_t otaRuntimeLastProgressMs();
uint32_t otaRuntimeLastBytes();
uint32_t otaRuntimeTimeoutMs();
bool otaRuntimeTryBegin(uint8_t owner, uint32_t timeoutMs);
void otaRuntimeMarkProgress(uint32_t bytes);
bool otaRuntimeEnd(uint8_t owner);
void otaRuntimeRestoreServices(const char* reason, bool restartRadar);
#endif

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
            DBG("ETH", "event=start uptime_ms=%lu", millis());
            ETH.setHostname(configManager.getConfig().hostname);
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("[ETH] Link UP");
            DBG("ETH", "event=connected uptime_ms=%lu link=%d", millis(), ETH.linkUp());
            g_ethFlap.onLinkUp(millis());
            ethConnected = true;
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.printf("[ETH] IP: %s  Speed: %dMbps  Duplex: %s\n",
                ETH.localIP().toString().c_str(),
                ETH.linkSpeed(),
                ETH.fullDuplex() ? "Full" : "Half");
            DBG("ETH", "event=got_ip uptime_ms=%lu speed_mbps=%d duplex=%s",
                millis(), ETH.linkSpeed(), ETH.fullDuplex() ? "full" : "half");
            ethGotIP = true;
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("[ETH] Link DOWN");
            DBG("ETH", "event=disconnected uptime_ms=%lu link=%d", millis(), ETH.linkUp());
            g_ethFlap.onLinkDown(millis());
            ethConnected = false;
            ethGotIP = false;
            break;
        case ARDUINO_EVENT_ETH_STOP:
            Serial.println("[ETH] Stopped");
            DBG("ETH", "event=stop uptime_ms=%lu", millis());
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
            reason, millis() / 1000, heapFreeUsable());
        systemLog.warn(String("Reboot inhibit: ") + reason + " suppressed");
        return;
    }
    // Drain web-originated MQTT work here, on the main-loop owner, before reboot.
    mqttService.processDeferredActions();
    preferences.putString("restart_cause", reason);
    preferences.putULong("last_uptime", millis() / 1000);
    preferences.putULong("last_heap", heapFreeUsable());
    preferences.putULong("last_maxalloc", heapLargestUsable());
    preferences.putULong("last_minheap", heapMinFreeUsable());
    DBG("SYSTEM", ">>> RESTART: %s (uptime %lus, heap %u/%u/%u)",
        reason, millis() / 1000, heapFreeUsable(), heapLargestUsable(), heapMinFreeUsable());
    delay(500);
    ESP.restart();
}

// -------------------------------------------------------------------------
// dev7 L3: OOM last resort — std::set_new_handler target
// -------------------------------------------------------------------------
// A throwing `new` could not be satisfied anywhere in the firmware (field
// coredump 2026-08-15: ESPAsyncWebServer header parsing). NOTHING here may
// allocate — no String, no NVS, no log, no safeRestart() (it does NVS puts
// and an MQTT drain). Stamp the RTC marker, then either restart cleanly or
// fall through to abort() for a panic + coredump (debug flag, OTA write in
// progress, or a restart loop caught by the uptime guard).
static void oomNewHandler() {
    uint32_t uptimeS = millis() / 1000;
    oomMarkerSet(g_oomMarker, uptimeS, heapFreeUsable(), heapLargestUsable());
    if (oomShouldRestart(g_oomRestartEnabled.load(), g_rebootInhibit.load(),
                         g_lastBootWasOomRestart, uptimeS)) {
        esp_restart();
    }
    abort();   // keep the pre-dev7 behaviour: panic + coredump
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
    DBG("CONFIG", "Updated %d zones", (int)zones.size());
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

// dev13: don't pay 8 kB of stack for a sensor that isn't there. On a radar-less
// unit LD2412Service::update() returns immediately on `!_radar`, so this task
// spun every 2 ms taking and releasing a mutex to do nothing — while holding
// 8192 B, roughly a quarter of the byte-addressable heap this board actually
// has free (25-38 kB measured). Idempotent and lazy so a radar that only
// answers later (post-OTA restore re-begins it) still gets its task. Every
// radarTaskHandle reader is already null-guarded.
static void ensureRadarTask(bool radarPresent) {
    if (radarTaskHandle || !radarPresent) return;
    xTaskCreatePinnedToCore(radarTask, "radar_task", 8192, nullptr, 2, &radarTaskHandle, 1);
}

// ETH link watchdog timeout — reboot if link stays down this long
constexpr unsigned long ETH_LINK_WATCHDOG_MS = 300000; // 5 minut

// Connectivity watchdog — monitors ETH link + MQTT health
void connectivityTask(void* param) {
    const TickType_t delayTicks = pdMS_TO_TICKS(INTERVAL_CONNECTIVITY_MS);
    int mqttFailCount = 0;
    const int MQTT_FAIL_THRESHOLD = 5;
    unsigned long ethDownSince = 0; // 0 = ETH is up
    uint32_t ethDownFlapMark = 0;   // flap count when this outage was first seen

    for (;;) {
        vTaskDelay(delayTicks);

        // Skip the watchdog cycle entirely while a /api/update is in flight.
        // The watchdog otherwise prints DBG, hits ETH.linkUp() / mqtt state,
        // and (without inhibit) could reboot mid-upload.
        if (g_runtimeOperationCoordinator.status().operation == RuntimeOperation::OTA) continue;

        // --- ETH link watchdog ---
        if (!ETH.linkUp()) {
            uint32_t flapNow = g_ethFlap.stats(millis()).downCount;
            if (ethDownSince == 0) {
                ethDownSince = millis();
                ethDownFlapMark = flapNow;
                DBG("CONN", "ETH link DOWN — watchdog started");
                systemLog.warn("ETH link DOWN");
            } else if (flapNow != ethDownFlapMark) {
                // A new outage began since the last sample, which means the
                // link came back up in between and this is a FLAPPING link,
                // not a dead one. Restarting the timer matters: a link down
                // 23.5 % of the time (measured in the field) makes six
                // consecutive unlucky samples a roughly daily event, and this
                // watchdog would then reboot a node whose link works.
                ethDownSince = millis();
                ethDownFlapMark = flapNow;
                DBG("CONN", "ETH link flapping (%lu outages) — watchdog timer restarted",
                    (unsigned long)flapNow);
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
            // This figure is quantised to the 60 s poll period and says only
            // "the link was down when I last looked". Report the driver's
            // measured total alongside it so the log cannot be misread as an
            // outage length — a 2 s PHY glitch used to print "restored after
            // 60s" and that number reached Home Assistant as fact.
            unsigned long downSec = (millis() - ethDownSince) / 1000;
            EthLinkFlapStats flap = g_ethFlap.stats(millis());
            DBG("CONN", "ETH link restored (unseen for <=%lu s; driver: %lu flaps, %lu ms down total)",
                downSec, (unsigned long)flap.downCount, (unsigned long)flap.downTotalMs);
            systemLog.info("ETH link back (unseen <=" + String(downSec) + "s, " +
                           String(flap.downCount) + " flaps/" +
                           String(flap.downTotalMs / 1000) + "s down since boot)");
            ethLinkDownSeconds = downSec;
            ethLinkRestoredNotify = true; // consumed by loop()
            ethDownSince = 0;
            ethDownFlapMark = 0;
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

    // Restore logu z předchozího bootu (RTC RAM), pak ring resetovat a
    // připojit jako mirror pro tento běh — dřív než cokoli začne logovat.
    // Carry the previous boot's watermark records into RAM before clearing the
    // RTC ring for this boot — after a panic they are the last thing the node
    // saw, and that is exactly when they matter.
    if (heapWmRingValid(g_heapWmRing) && heapWmRingCount(g_heapWmRing) > 0) {
        memcpy(&g_heapWmPrevBoot, &g_heapWmRing, sizeof(g_heapWmPrevBoot));
        g_heapWmPrevBootValid = true;
    }
    heapWmRingInit(g_heapWmRing);
    if (logRingValid(g_rtcLog)) systemLog.restorePrevBoot(g_rtcLog);
    logRingInit(g_rtcLog);
    systemLog.attachRtcMirror(&g_rtcLog);

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
    Serial.println(">> " FW_VERSION_BINARY_MARKER);
    // Configured flash size (from build header) — makes the flashed variant
    // visible in the boot log. NOTE: this is the BUILD's flash_size, not the
    // physical chip (a 16MB build on an 8MB chip bootloops in the bootloader
    // before reaching here). Verify the chip with `esptool flash_id` (4017=8MB,
    // 4018=16MB) before flashing — see platformio.ini env warning.
    Serial.printf(">> Flash size (build cfg): %u MB\n", ESP.getFlashChipSize() / (1024U * 1024U));

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
    g_ethFlap.begin(millis());   // observation window opens with the handler
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

    // dev7 L3: an OOM new-handler restart cannot write NVS (restart_cause stays
    // "none", reset reason reads as a plain software reset) — the RTC marker is
    // the only record. Fold it into this boot's entry and remember it for the
    // restart-loop guard in oomNewHandler().
    const char* uptimeSrc = rtcValid ? "rtc" : "nvs";
    if (oomMarkerValid(g_oomMarker)) {
        g_lastBootWasOomRestart = true;
        prevRestartCause = "oom_gate heap=" + String(g_oomMarker.freeBytes) +
                           "/" + String(g_oomMarker.largestBytes);
        crashUptime = g_oomMarker.uptimeS;
        uptimeSrc = "oom_marker";
    }
    oomMarkerClear(g_oomMarker);   // also scrubs power-on RTC garbage

    JsonObject entry = arr.add<JsonObject>();
    entry["reason"] = reasonStr;
    entry["cause"] = prevRestartCause;
    entry["uptime"] = crashUptime;
    entry["uptime_src"] = uptimeSrc;
    entry["ts"] = millis();

    while (arr.size() > 10) arr.remove(0);

    String newHistory;
    serializeJson(historyDoc, newHistory);
    preferences.putString("reset_history", newHistory);
    preferences.putString("restart_cause", "none");
    DBG("SYSTEM", "Reset reason: %s, cause: %s", reasonStr.c_str(), prevRestartCause.c_str());
    systemLog.warn("System restart: " + reasonStr + " (" + prevRestartCause + ")");

    // dev8: dirty boot (panic/WDT/brownout/oom_gate) → queue a Telegram notice
    // with everything the previous run left behind; the loop delivers it once
    // the network is up. Clean boots (OTA, user restart, power-cycle) stay quiet.
    if (bootOutageIsDirty(reasonStr.c_str(), prevRestartCause.c_str())) {
        BootOutageInfo oi;
        oi.resetReason  = reasonStr.c_str();
        oi.restartCause = prevRestartCause.c_str();
        oi.fwVersion    = FW_VERSION;
        oi.uptimeS      = crashUptime;
        oi.prevHeap     = preferences.getULong("last_heap", 0);
        oi.prevMaxAlloc = preferences.getULong("last_maxalloc", 0);
        oi.prevMinHeap  = preferences.getULong("last_minheap", 0);
        size_t cdAddr = 0, cdSize = 0;
        oi.coredumpPresent = (esp_core_dump_image_get(&cdAddr, &cdSize) == ESP_OK && cdSize > 0);
        char noticeBuf[400];
        formatBootOutageNotice(noticeBuf, sizeof(noticeBuf), oi);
        g_bootOutageNotice = noticeBuf;
        formatBootOutageEventJson(noticeBuf, sizeof(noticeBuf), oi);
        g_bootOutageEventJson = noticeBuf;
    }

    // dev7: web low-heap gate config + OOM last-resort handler. Registered
    // after the marker read above so the loop guard sees this boot's origin.
    {
        HeapGateConfig gcfg;   // dev12: defaults are byte-addressable-heap bytes
        gcfg.enabled = preferences.getBool("web_gate_en", true);
        // dev12 key rename. The dev7 keys (web_gate_close/web_gate_open) held
        // kB values calibrated against MALLOC_CAP_INTERNAL readings, ~42 kB too
        // high for this board; reusing them would let a stale NVS entry hold the
        // gate permanently closed against honest numbers. Abandon, don't reuse.
        // Defaults come from the struct so the calibration lives in one place.
        gcfg.closeFreeBytes    = preferences.getUInt("wg8_close",  gcfg.closeFreeBytes    / 1024) * 1024;
        gcfg.openFreeBytes     = preferences.getUInt("wg8_open",   gcfg.openFreeBytes     / 1024) * 1024;
        gcfg.closeLargestBytes = preferences.getUInt("wg8_lclose", gcfg.closeLargestBytes / 1024) * 1024;
        gcfg.openLargestBytes  = preferences.getUInt("wg8_lopen",  gcfg.openLargestBytes  / 1024) * 1024;
        g_webHeapGate.configure(gcfg);
        g_oomRestartEnabled.store(preferences.getBool("oom_restart_en", true));
        std::set_new_handler(oomNewHandler);
        DBG("SYSTEM", "Web heap gate: %s free %u/%ukB largest %u/%ukB (8bit heap), OOM restart: %s",
            gcfg.enabled ? "on" : "off",
            (unsigned)(gcfg.closeFreeBytes / 1024), (unsigned)(gcfg.openFreeBytes / 1024),
            (unsigned)(gcfg.closeLargestBytes / 1024), (unsigned)(gcfg.openLargestBytes / 1024),
            g_oomRestartEnabled.load() ? "on" : "off");
        // One-off boot audit: how far the pre-dev12 readings were off on THIS
        // board. Same line proves the fix took effect on any future hardware.
        {
            HeapReading hr = heapReadingNow();
            DBG("SYSTEM", "Heap 8bit free=%u largest=%u | internal free=%u largest=%u | unusable=%u%s",
                (unsigned)hr.freeUsable, (unsigned)hr.largestUsable,
                (unsigned)hr.freeInternal, (unsigned)hr.largestInternal,
                (unsigned)heapUnusableInternalBytes(hr),
                heapInternalIsMisleading(hr) ? " (INTERNAL MISLEADING)" : "");
        }
    }

    uint8_t minGate = preferences.getUInt("radar_min", 0);
    uint8_t maxGate = preferences.getUInt("radar_max", 13);

    bool radarPresent = radar.begin(Serial2, minGate, maxGate);
    if (!radarPresent) {
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

    // Run radar update in a dedicated task on core 1 — only if a radar answered
    ensureRadarTask(radarPresent);
    if (!radarPresent) {
        systemLog.warn("Radar absent: radar task not started, 8 kB stack reclaimed");
    }

    // --- Start Ethernet ---
    Serial.println("[ETH] Initializing LAN8720A...");
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, -1, ETH_CLK_MODE);
#else
    ETH.begin(ETH_PHY_ADDR, -1, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_TYPE, ETH_CLK_MODE);
#endif

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
        // dev7 L1b: SSE reconnect storms (browser tabs + HA after a link flap)
        // are a heap spike — refuse new streams while the heap gate is closed
        // and cap concurrent clients. onConnect fires BEFORE the client is
        // added to the list, so count() excludes the one connecting here.
        if (g_webHeapGate.isClosed() || events.count() >= WEB_SSE_MAX_CLIENTS) {
            client->close();
            return;
        }
        if (client->lastId()) {
            DBG("SSE", "Client reconnected! Last message ID: %u", client->lastId());
        }
        client->send("hello!", NULL, millis(), 1000);
    });
    server.addHandler(&events);
#endif

    // WDT after ETH init
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    {
        esp_task_wdt_config_t wdtCfg = {};
        wdtCfg.timeout_ms = WDT_TIMEOUT_SECONDS * 1000;
        wdtCfg.idle_core_mask = 0;      // idle tasks not watched (matches 2.x behaviour)
        wdtCfg.trigger_panic = true;
        // Arduino 3.x core TWDT typically already initialized → init returns ESP_ERR_INVALID_STATE;
        // reconfigure in that case so the 60s timeout is actually applied.
        if (esp_task_wdt_init(&wdtCfg) != ESP_OK) {
            esp_task_wdt_reconfigure(&wdtCfg);
        }
    }
#else
    esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
#endif
    esp_task_wdt_add(NULL);

    // --- Web Server Routes ---
#ifndef LITE_BUILD
    WebRoutes::Dependencies deps = {
        .server = &server,
        .events = &events,
        .preferences = &preferences,
        .radar = &radar,
        .mqttService = &mqttService,
        .runtimeOperationCoordinator = &g_runtimeOperationCoordinator,
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
        .mlFeedbackStore = &mlFeedbackStore,
        #else
        .csiService = nullptr,
        .mlFeedbackStore = nullptr,
        #endif
    };

    WebRoutes::setup(deps);
    server.begin();
#endif

    // Init services
    g_myDeviceId = configManager.getConfig().mqtt_id; // For supervision heartbeat
    mqttService.setRuntimeOperationCoordinator(&g_runtimeOperationCoordinator);
    if (configManager.getConfig().mqtt_enabled) {
        // M-5: a MQTT alarm PIN and Home Assistant control are mutually exclusive
        // (HA sends bare ARM/DISARM that the PIN guard rejects). Flag the conflict.
        if (preferences.getString("sec_mqtt_pin", "").length() > 0) {
            Serial.println("[SECURITY] NOTE: MQTT alarm PIN is set — Home Assistant alarm control (bare ARM/DISARM) will be rejected. Use the PIN or HA control, not both.");
        }
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
            } else if (strcmp(topic, t.preset_set) == 0) {
                SecPreset preset;
                if (parseSecPreset(payload, preset)) {
                    securityMonitor.applySecurityPreset(preset);
                    preferences.putUInt("sec_preset", (uint32_t)preset);
                    mqttService.publishPresetState(secPresetName(preset));
                }
            } else if (strcmp(topic, t.cmd_dyn_bg) == 0) {
                radar.startCalibration();
            } else if (strcmp(topic, t.alarm_set) == 0) {
                const String mqttPin = preferences.getString("sec_mqtt_pin", "");
                MqttArmResult r = MqttCommandRouter::evaluateArmCommand(
                    payload, mqttPin.c_str(), mqttCmdLockout, millis());
                switch (r.decision) {
                    case MqttArmDecision::Accepted:
                        if (r.command == MqttArmCommand::ArmAway) securityMonitor.setArmed(true, false, false);
                        else if (r.command == MqttArmCommand::ArmHome) securityMonitor.setArmed(true, false, true);
                        else if (r.command == MqttArmCommand::Disarm) securityMonitor.setArmed(false);
                        break;
                    case MqttArmDecision::RejectedLockedOut:
                        DBG("SecMon", "MQTT alarm cmd rejected — PIN locked out");
                        break;
                    case MqttArmDecision::RejectedNoPin:
                        DBG("SecMon", "MQTT alarm cmd '%s' rejected — PIN required", payload);
                        break;
                    case MqttArmDecision::RejectedWrongPin:
                        DBG("SecMon", "MQTT alarm cmd rejected — wrong PIN");
                        break;
                    case MqttArmDecision::RejectedUnknownCommand:
                        break;
                }
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
    mlFeedbackStore.begin(fsOk);
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

    // v5.5: the security preset is a BASELINE bundle; the explicit per-parameter
    // config below overrides it. Apply the preset only if the user actually chose
    // one, so devices that never touched presets keep their persisted/default
    // tunables — no silent alarm-behavior change on upgrade.
    if (preferences.isKey("sec_preset")) {
        uint32_t presetRaw = preferences.getUInt("sec_preset", (uint32_t)SecPreset::HOME);
        if (presetRaw > (uint32_t)SecPreset::PARANOID) {
            presetRaw = (uint32_t)SecPreset::HOME;
        }
        securityMonitor.applySecurityPreset((SecPreset)presetRaw);
    }

    // Preset-controlled params: an explicitly persisted value overrides the preset
    // baseline; unset -> keep preset/compile default (== member default, so a
    // non-preset device is byte-for-byte unaffected).
    if (preferences.isKey("sec_entry_dl"))
        securityMonitor.setEntryDelay(preferences.getULong("sec_entry_dl", DEFAULT_ENTRY_DELAY_MS));
    if (preferences.isKey("sec_alarm_en"))
        securityMonitor.setAlarmEnergyThreshold(preferences.getUChar("sec_alarm_en", DEFAULT_ALARM_ENERGY_THRESHOLD));
    if (preferences.isKey("sec_pet"))
        securityMonitor.setPetImmunity((uint8_t)preferences.getUInt("sec_pet", 0));
    if (preferences.isKey("sec_corrob"))
        securityMonitor.setCorroborationEnabled(preferences.getBool("sec_corrob", false));

    // Params the preset does not touch: always loaded.
    securityMonitor.setExitDelay(preferences.getULong("sec_exit_dl", DEFAULT_EXIT_DELAY_MS));
    securityMonitor.setDisarmReminderEnabled(preferences.getBool("sec_dis_rem", false));
    securityMonitor.setTriggerTimeout(preferences.getULong("sec_trig_to", DEFAULT_TRIGGER_TIMEOUT_MS));
    securityMonitor.setAutoRearm(preferences.getBool("sec_auto_rearm", true));
    securityMonitor.setCrossModalEnabled(preferences.getBool("sec_xmodal", true));
    securityMonitor.setCorroborationWindowMs(preferences.getULong("sec_corrob_ms", 8000));
    if (configManager.getConfig().mqtt_enabled) {
        mqttService.publishPresetState(
            secPresetName(securityMonitor.getSecurityPreset()));
    }
    securityMonitor.setSirenPin(SIREN_PIN);
    if (preferences.getBool("sec_armed", false)) {
        bool homeMode = preferences.getBool("sec_home_mode", false);
        securityMonitor.setArmed(true, false, homeMode);
    }

#ifndef LITE_BUILD
    ArduinoOTA.onStart([]() {
        otaRuntimeEnd(OTA_OWNER_ESPOTA_PREPARE);
        if (!otaRuntimeTryBegin(OTA_OWNER_ESPOTA, 180000)) {
            systemLog.warn(String("ArduinoOTA rejected while runtime operation is ") +
                runtimeOperationText(g_runtimeOperationCoordinator.status().operation));
            otaRuntimeRestoreServices("espota_start_rejected", true);
            return;
        }
        otaRuntimeMarkProgress(0);
        g_espotaMaintenance.store(true);
#ifdef USE_CSI
        csiService.wifiDownForOta();   // single-home: drop CSI WiFi so we're not dual-homed during OTA
#endif
        mqttService.publishMaintenance(true);
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else
            type = "filesystem";
        Serial.println("Start updating " + type);
        // Save config snapshot before OTA overwrites flash
        configSnapshot.saveSnapshot(&preferences, FW_VERSION, "ota_arduino");
        if (radarTaskHandle && eTaskGetState(radarTaskHandle) != eSuspended) vTaskSuspend(radarTaskHandle);
        radar.stop();
        if (telegramBot.isEnabled()) {
            Serial.println("[OTA] Attempting Telegram notification...");
            telegramBot.sendMessage("⚠️ OTA Update Started...");
        }
    });
    ArduinoOTA.onEnd([]() {
        mqttService.publishMaintenance(false);
        g_espotaMaintenance.store(false);
        g_espotaMaintenanceUntilMs.store(0);
        otaRuntimeEnd(OTA_OWNER_ESPOTA);
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        otaRuntimeMarkProgress(total > 0 ? progress : 0);
    });
    ArduinoOTA.onError([](ota_error_t) {
        if (otaRuntimeOwner() == OTA_OWNER_ESPOTA) {
            otaRuntimeRestoreServices("arduino_ota_error", true);
            otaRuntimeEnd(OTA_OWNER_ESPOTA);
        }
        systemLog.error("ArduinoOTA failed; runtime services restored");
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
        csiService.setRuntimeOperationCoordinator(&g_runtimeOperationCoordinator);
        // Apply NVS-stored runtime config BEFORE begin so allocations use right window size
        csiService.setWindowSize(configManager.getConfig().csi_window);
        csiService.setThreshold(configManager.getConfig().csi_threshold);
        csiService.setHysteresis(configManager.getConfig().csi_hysteresis);
        csiService.setPublishInterval(configManager.getConfig().csi_publish_ms);

        // Traffic generator tuning from NVS
        if (preferences.isKey("csi_tport")) csiService.setTrafficPort(preferences.getUShort("csi_tport", 7));
        if (preferences.isKey("csi_ticmp")) csiService.setTrafficICMP(preferences.getBool("csi_ticmp", true));
        if (preferences.isKey("csi_tpps"))  csiService.setTrafficRate(preferences.getUInt("csi_tpps", 100));

        // csi9: ML MLP runtime settings from NVS (defaults: enabled=true, threshold=0.50)
        if (preferences.isKey("csi_ml_en"))  csiService.setMlEnabled(preferences.getBool("csi_ml_en", true));
        if (preferences.isKey("csi_ml_thr")) csiService.setMlThreshold(preferences.getFloat("csi_ml_thr", 0.50f));
        // #13: adaptive-threshold quantile (P95 default / P99) from NVS
        if (preferences.isKey("csi_adapt_pct")) csiService.setAdaptivePercentile(preferences.getFloat("csi_adapt_pct", 0.95f));

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

#ifndef LITE_BUILD
const char* otaRuntimeOwnerName(uint8_t owner) {
    switch (owner) {
        case OTA_OWNER_HTTP: return "http_multipart";
        case OTA_OWNER_PULL: return "pull";
        case OTA_OWNER_ESPOTA_PREPARE: return "espota_prepare";
        case OTA_OWNER_ESPOTA: return "espota";
        default: return "none";
    }
}

uint8_t otaRuntimeOwner() {
    RuntimeOperationStatus status = g_runtimeOperationCoordinator.status();
    return status.operation == RuntimeOperation::OTA ? status.ownerId : OTA_OWNER_NONE;
}

bool otaRuntimeTransferActive() {
    uint8_t owner = otaRuntimeOwner();
    return owner != OTA_OWNER_NONE && owner != OTA_OWNER_ESPOTA_PREPARE;
}

uint32_t otaRuntimeLastProgressMs() {
    return g_runtimeOperationCoordinator.status().lastProgressMs;
}

uint32_t otaRuntimeLastBytes() {
    return g_otaRuntimeLastBytes.load();
}

uint32_t otaRuntimeTimeoutMs() {
    return g_runtimeOperationCoordinator.status().timeoutMs;
}

bool otaRuntimeTryBegin(uint8_t owner, uint32_t timeoutMs) {
    if (owner == OTA_OWNER_NONE ||
        !g_runtimeOperationCoordinator.tryBegin(
            RuntimeOperation::OTA, millis(), timeoutMs, owner)) {
        return false;
    }
    g_otaRuntimeLastBytes.store(0);
    return true;
}

void otaRuntimeMarkProgress(uint32_t bytes) {
    uint8_t owner = otaRuntimeOwner();
    if (owner != OTA_OWNER_NONE && g_runtimeOperationCoordinator.markProgress(
            RuntimeOperation::OTA, millis(), owner)) {
        g_otaRuntimeLastBytes.store(bytes);
    }
}

bool otaRuntimeEnd(uint8_t owner) {
    if (owner == OTA_OWNER_NONE ||
        !g_runtimeOperationCoordinator.finish(RuntimeOperation::OTA, owner)) {
        return false;
    }
    g_otaRuntimeLastBytes.store(0);
    return true;
}

void otaRuntimeRestoreServices(const char* reason, bool restartRadar) {
    if (Update.isRunning()) {
        Update.abort();
    }
#ifdef USE_CSI
    csiService.wifiUpAfterOta();   // restore CSI WiFi if the OTA window closed without a reboot
#endif
    mqttService.requestMaintenancePublish(false);
    if (restartRadar) {
        uint8_t minGate = preferences.getUInt("radar_min", 0);
        uint8_t maxGate = preferences.getUInt("radar_max", 13);
        ensureRadarTask(radar.begin(Serial2, minGate, maxGate));
    }
    if (radarTaskHandle && eTaskGetState(radarTaskHandle) == eSuspended) {
        vTaskResume(radarTaskHandle);
    }
    g_espotaMaintenance.store(false);
    g_espotaMaintenanceUntilMs.store(0);
    systemLog.warn(String("OTA runtime services restored: ") + (reason ? reason : "unknown"));
}

static void handleOtaRuntimeWatchdog(unsigned long now) {
    RuntimeOperationStatus timedOut;
    if (!g_runtimeOperationCoordinator.checkTimeout(
            RuntimeOperation::OTA, now, &timedOut) ||
        timedOut.operation != RuntimeOperation::OTA) return;

    uint8_t owner = timedOut.ownerId;
    const char* ownerName = otaRuntimeOwnerName(owner);
    DBG("OTA", "Runtime watchdog timeout for %s after %lu ms",
        ownerName, now - timedOut.lastProgressMs);
    systemLog.error(String("OTA timeout: ") + ownerName);
    preferences.putString("ota_phase", "failed");
    preferences.putString("ota_msg", String("runtime timeout: ") + ownerName);
    preferences.putInt("ota_err", -6);
    otaRuntimeRestoreServices("watchdog_timeout", owner == OTA_OWNER_ESPOTA);
    g_otaRuntimeLastBytes.store(0);
}

static void clearEspotaMaintenanceMode() {
    otaRuntimeRestoreServices("espota_window_closed", false);
    otaRuntimeEnd(OTA_OWNER_ESPOTA_PREPARE);
    systemLog.info("ESPOTA maintenance window closed");
}

static void handleEspotaMaintenance(unsigned long now) {
    if (g_espotaPrepareRequested.exchange(false)) {
        uint32_t seconds = g_espotaMaintenanceSeconds.load();
        if (seconds < 30) seconds = 30;
        if (seconds > 600) seconds = 600;

        ArduinoOTA.end();
        delay(20);
        ArduinoOTA.begin();

#ifdef USE_CSI
        csiService.wifiDownForOta();   // single-home: drop CSI WiFi so we're not dual-homed during OTA
#endif
        mqttService.publishMaintenance(true);
        if (radarTaskHandle && eTaskGetState(radarTaskHandle) != eSuspended) {
            vTaskSuspend(radarTaskHandle);
        }

        g_espotaMaintenance.store(true);
        g_espotaMaintenanceUntilMs.store(now + seconds * 1000UL);
        systemLog.warn("ESPOTA maintenance window opened for " + String(seconds) + "s");
        DBG("OTA", "ESPOTA maintenance window opened for %us", (unsigned)seconds);
    }

    if (g_espotaMaintenance.load() && !otaRuntimeTransferActive()) {
        unsigned long until = g_espotaMaintenanceUntilMs.load();
        if (until != 0 && (long)(now - until) >= 0) {
            clearEspotaMaintenanceMode();
        }
    }
}
#endif

// -------------------------------------------------------------------------
// Loop
// -------------------------------------------------------------------------
void loop() {
    unsigned long now = millis();
    esp_task_wdt_reset();

    // v5.0.2-rc1: RTC uptime tracker — every loop tick. Survives Task WDT/panic
    // so reset_history gets ~1s resolution on crash time instead of hourly.
    rtc_last_uptime_s = now / 1000;

    // dev17: the watermark already records the worst moment since boot; what was
    // missing is WHEN it moved and what was running then. Sampled here rather
    // than over HTTP — an /api/health request allocates more than the ~40 kB
    // spike being hunted, and at 15 s the field sampler never once caught it.
    // The line lands in the RTC-mirrored log, so it survives a panic reboot.
    static uint32_t lastHeapTripwireMs = 0;
    if ((uint32_t)(now - lastHeapTripwireMs) >= 100) {
        lastHeapTripwireMs = now;
        if (g_heapWatermarkTripwire.evaluate(heapMinFreeUsable())) {
            int rssiNow = 0;
#ifdef USE_CSI
            rssiNow = csiService.getWifiRSSI();
#endif
            HeapWatermarkRecord wm{};
            wm.uptimeS        = now / 1000;
            wm.prevWatermark  = g_heapWatermarkTripwire.previousWatermark();
            wm.watermark      = g_heapWatermarkTripwire.lastRecorded();
            wm.largest        = heapLargestUsable();
            wm.freeNow        = heapFreeUsable();
            wm.mqttReconnects = mqttService.getReconnectTotal();
            wm.rssi           = (int16_t)rssiNow;
            wm.discoveryIndex = (int16_t)mqttService.discoveryIndex();
            wm.mqttConnected  = mqttService.connected() ? 1 : 0;
            wm.runtimeOp      = (uint8_t)g_runtimeOperationCoordinator.status().operation;
            if (auto* gs = GatedAsyncWebServer::active()) wm.inFlight = gs->admission().inFlight();
            wm.sseClients = (uint8_t)events.count();
            wm.sseWaiting = events.avgPacketsWaiting();
            wm.activityMask = g_heapActivityMask.load();
            heapWmRingAppend(g_heapWmRing, wm);
            // DBG only — deliberately NOT systemLog: that path feeds the shared
            // 20-slot RTC ring this record was moved out of.
            DBG("HEAP", "heapmin %u->%u lg=%u fr=%u mq=%d/%u d=%d op=%u r=%d",
                (unsigned)wm.prevWatermark, (unsigned)wm.watermark,
                (unsigned)wm.largest, (unsigned)wm.freeNow,
                wm.mqttConnected, (unsigned)wm.mqttReconnects,
                wm.discoveryIndex, (unsigned)wm.runtimeOp, rssiNow);
        }
    }

#ifndef LITE_BUILD
    handleOtaRuntimeWatchdog(now);
    handleEspotaMaintenance(now);
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

    if (configManager.getConfig().mqtt_enabled) {
        HeapActivityScope activity(HeapActivity::MqttLoop);
        mqttService.update();
    }
#ifdef USE_CSI
    csiService.processDeferredActions();
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
            // Radar-less kus: UART config dotaz na neinicializovaný/odpojený
            // radar umí skončit LoadProhibited v LD2412::getAckNonBlocking
            // (coredump 2026-07-18) — přeskočit, není co ověřovat.
            if (radar.isRadarConnected()) {
                uint8_t expectedMin = preferences.getUInt("radar_min", 0);
                uint8_t expectedMax = preferences.getUInt("radar_max", 13);
                if (!radar.verifyGateConfig(expectedMin, expectedMax)) {
                    eventLog.addEvent(EVT_SYSTEM, 0, 0, "Gate config reverted by FW");
                }
            }
        }
    }

    securityMonitor.update();
    {
        HeapActivityScope activity(HeapActivity::TelegramLoop);
        telegramBot.update();
    }
    eventLog.flush();

    // dev8: MQTT outage event — retried every pass until the (cheap,
    // non-retained) publish lands; HA forwards it to Telegram.
    if (g_bootOutageEventJson.length() && mqttService.connected()) {
        if (mqttService.publish(mqttService.getTopics().system_outage,
                                g_bootOutageEventJson.c_str(), false)) {
            g_bootOutageEventJson = "";
        }
    }

    // dev8: deliver the boot outage notice once the network is up. MQTT
    // connectivity is the readiness proxy; Telegram TLS may lag it, so retry
    // every 30 s and drop after 5 attempts (or immediately when Telegram is
    // disabled) instead of holding the String forever.
    if (g_bootOutageNotice.length() && mqttService.connected()) {
        static uint8_t outageAttempts = 0;
        static unsigned long outageLastTry = 0;
        if (outageLastTry == 0 || now - outageLastTry >= 30000) {
            outageLastTry = now;
            outageAttempts++;
            bool sent = telegramBot.isEnabled() && notificationService.sendTelegram(g_bootOutageNotice);
            if (sent || outageAttempts >= 5 || !telegramBot.isEnabled()) {
                g_bootOutageNotice = "";
            }
        }
    }

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

        // dev7 L2: keep the heap gate honest without traffic and log its
        // transitions. The CLOSE snapshot answers "what was eating the heap" —
        // systemLog mirrors into the RTC ring, so it survives a follow-up
        // crash/restart. Logging allocates, but at the close threshold
        // (14 kB usable free, dev12) that is still safe — this runs on the
        // loop task, not in the OOM handler.
        {
            static bool lastGateClosed = false;
            static unsigned long gateClosedSinceMs = 0;
            g_webHeapGate.probe(heapFreeUsable(), heapLargestUsable());
            bool gateClosed = g_webHeapGate.isClosed();
            if (gateClosed != lastGateClosed) {
                if (gateClosed) {
                    gateClosedSinceMs = now;
                    String snap = "Web heap gate CLOSED: free=" + String(heapFreeUsable()) +
                                  " largest=" + String(heapLargestUsable());
                    #ifndef LITE_BUILD
                    snap += " sse=" + String(events.count());
                    #endif
                    snap += " mqtt=" + String(mqttService.connected() ? 1 : 0);
                    systemLog.warn(snap);
                } else {
                    unsigned long closedForS = gateClosedSinceMs ? (now - gateClosedSinceMs) / 1000 : 0;
                    systemLog.info("Web heap gate reopened (rejected " +
                                   String(g_webHeapGate.rejectsTotal()) + " total)");
                    // dev8: heap-pressure episode survived without a crash —
                    // report it while the evidence is fresh. Sent on REOPEN,
                    // not close: at close the heap can't afford Telegram TLS.
                    char gateEv[200];
                    formatHeapGateEventJson(gateEv, sizeof(gateEv), closedForS,
                                            g_webHeapGate.rejectsTotal(),
                                            heapFreeUsable(), heapMinFreeUsable());
                    if (mqttService.connected()) {
                        mqttService.publish(mqttService.getTopics().system_outage, gateEv, false);
                    }
                    if (telegramBot.isEnabled()) {
                        notificationService.sendTelegram(
                            String("⚠️ Heap-pressure episode survived\n") +
                            "gate closed for: " + String(closedForS) + "s\n" +
                            "rejected connections (total): " + String(g_webHeapGate.rejectsTotal()) + "\n" +
                            "heap now: " + String(heapFreeUsable()) +
                            " (min " + String(heapMinFreeUsable()) + ")");
                    }
                }
                lastGateClosed = gateClosed;
            }
        }
        // RSSI anomaly detection removed — Ethernet doesn't have RSSI

#ifdef USE_CSI
        // CSI data health: WiFi associated but no CSI frames for >15s = detection
        // starved (weak signal / AP issue). Log lost/restored transitions to the
        // web SYS log and surface state for /api/health + metrics. Recovers on its
        // own when frames resume (unlike the radar latch which needs a reboot).
        {
            static bool csiStarved = false;
            static unsigned long csiLastDataMs = 0;
            static unsigned long csiRecoverSinceMs = 0;
            int csiRssi = csiService.getWifiRSSI();
            bool csiAssociated = (csiRssi != 0);   // getWifiRSSI()==0 → not WL_CONNECTED
            float csiPps = csiService.getPacketRate();
            if (!csiAssociated) {
                csiLastDataMs = 0;                 // restart grace clock for next association
                csiRecoverSinceMs = 0;
            } else {
                if (csiLastDataMs == 0) csiLastDataMs = now;   // grace starts at association
                if (csiPps > 0.0f) csiLastDataMs = now;
            }
            bool starvedNow = csiAssociated && (now - csiLastDataMs > TIMEOUT_CSI_DATA_STARVE_MS);
            if (starvedNow && !csiStarved) {
                systemLog.warn("CSI data lost — RSSI " + String(csiRssi) + " dBm (reason " +
                               String((int)csiService.getLastDisconnectReason()) + ")");
                csiStarved = true;
                csiRecoverSinceMs = 0;
            } else if (csiStarved) {
                // Recovery needs SUSTAINED data flow, not one stray packet — otherwise a
                // marginal link flaps lost/restored and floods the log. Require pps>0
                // continuously for TIMEOUT_CSI_DATA_RECOVER_MS before clearing.
                if (csiPps > 0.0f) {
                    if (csiRecoverSinceMs == 0) csiRecoverSinceMs = now;
                    if (now - csiRecoverSinceMs >= TIMEOUT_CSI_DATA_RECOVER_MS) {
                        systemLog.info("CSI data restored — RSSI " + String(csiRssi) + " dBm, " +
                                       String(csiPps, 1) + " pkt/s");
                        csiStarved = false;
                        csiRecoverSinceMs = 0;
                    }
                } else {
                    csiRecoverSinceMs = 0;   // gap in data → restart sustained-recovery timer
                }
            }
            g_csiDataStarved.store(csiStarved);
            securityMonitor.setCsiDataOk(!csiStarved);
        }
#endif
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
    bool dmsBlocked = g_runtimeOperationCoordinator.status().operation == RuntimeOperation::OTA ||
                      g_rebootInhibit.load();
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
    // v5.4.1: reset až po REÁLNÉM publishi — _lastPublish je na bootu
    // inicializovaný na millis(), takže samotné "not stale" nulovalo čítač
    // pár sekund po každém startu a cap DMS_MAX_RESTARTS nikdy nezafungoval
    // (8-10x restart smyčka při výpadku brokeru na obou test nodech).
    if (dmsShouldResetCounter(dmsRestarts, mqttService.publishedSinceBoot(), dmsPublishStale)) {
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
    bool sseSkipForOta =
        g_runtimeOperationCoordinator.status().operation == RuntimeOperation::OTA;
    // dev13: an undrained SSE client is what collapses this heap. The library
    // bounds its per-client queue by MESSAGE COUNT, not by bytes, and this
    // payload is ~1.5 kB every 250 ms — so at the library default of 32 one
    // stalled dashboard tab pins ~48 kB in separately allocated Strings, more
    // than the whole free heap on this board. Field log at the collapse:
    // "gate CLOSED free=10800 largest=5876 sse=1" then "oom_gate heap=916/148".
    // The accept gate cannot help, the client is already connected. Two bounds
    // now: the count cap is pinned to 8 where the compiler actually sees it
    // (platformio.ini), and this gate stops producing entirely once a client
    // falls behind — a dashboard only ever wants the newest reading, so drop
    // the tick rather than queue it behind one that never went out.
    bool sseBacklogged = (events.count() > 0 &&
                          events.avgPacketsWaiting() >= WEB_SSE_MAX_BACKLOG);
    if (sseBacklogged) g_sseBacklogSkips.fetch_add(1, std::memory_order_relaxed);
    // dev19: this gate used to drop the telemetry tick silently whenever the
    // heap dipped, so a dashboard going quiet looked the same as a node with
    // nothing to say. Count it, and only when the heap is the actual reason —
    // OTA and backlog have their own accounting above.
    bool sseDue = !sseSkipForOta && !sseBacklogged && now - lastSSE > INTERVAL_SSE_UPDATE_MS;
    uint32_t sseFree = heapFreeUsable();
    if (sseDue && sseFree < HEAP_MIN_FOR_PUBLISH) {
        g_sseHeapSkips.record((uint32_t)now, sseFree);
    }
    if (sseDue && sseFree >= HEAP_MIN_FOR_PUBLISH) {
        HeapActivityScope activity(HeapActivity::SsePublish);
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

            // Fusion state in SSE telemetry (#8 fusion panel)
            if (securityMonitor.isFusionActive()) {
                JsonObject fusion = doc["fusion"].to<JsonObject>();
                fusion["presence"]   = securityMonitor.isFusionPresence();
                fusion["confidence"] = securityMonitor.getFusionConfidence();
                fusion["source"]     = securityMonitor.getFusionSourceStr();
                uint8_t fsrc = securityMonitor.getFusionSource();
                fusion["radar"] = (fsrc & FUSION_RADAR) != 0;
                fusion["csi"]   = (fsrc & FUSION_CSI) != 0;
                fusion["ml"]    = (fsrc & FUSION_ML) != 0;
                // radar N/A on CSI-only nodes (radar-less) — panel greys the bar
                fusion["radar_present"] = !securityMonitor.isRadarMonitoringDisabled();
                // bar levels 0-100: radar = current moving energy (already in doc
                // from radar.getTelemetryJson), csi = composite score, ml = probability
                fusion["radar_lvl"] = doc["moving_energy"].as<int>();
                fusion["csi_lvl"]   = (int)(csiService.getCompositeScore() * 100.0f);
                fusion["ml_lvl"]    = (int)(csiService.getMlProbability() * 100.0f);
                char rbuf[96];
                fusionReasonStr(fsrc, securityMonitor.getFusionConfidence(), rbuf, sizeof(rbuf));
                fusion["reason"] = rbuf;
            }
        }
        #endif

        // Buffer must fit full telemetry JSON. Worst case ~1.55 KB: radar incl.
        // eng_mode gate arrays ~980 B + CSI with ML + learning ~395 B + fusion
        // block ~240 B (v5.6.0). v5.7.0-dev2: 2048 (z 1536 — fusion blok sezral
        // rezervu). Overflow je tichy: serializeJson vrati sizeof(buf), guard
        // nize zahodi KAZDY event a cely dashboard zamrzne (pre-v4.1.3 mod).
        char sseBuf[2048];
        size_t sseLen = serializeJson(doc, sseBuf, sizeof(sseBuf));
        if (sseLen > 0 && sseLen < sizeof(sseBuf)) {
            events.send(sseBuf, "telemetry", millis());
        }
    }
#endif

    // =====================================================================
    // MQTT Publish-on-Change with Deadband (3-tier system)
    // =====================================================================

    uint32_t publishFree = heapFreeUsable();
    if (publishFree < HEAP_MIN_FOR_PUBLISH) {
        // dev19: read the free heap ONCE and report that same value. The old
        // line printed nothing at all, and re-reading here would report a heap
        // that had already recovered — which is precisely how this dip stayed
        // invisible for a week. in_flight answers whether an AsyncTCP request
        // was being served at the moment loop() saw the heap collapse.
        if (g_mqttHeapSkips.record((uint32_t)now, publishFree)) {
            uint8_t inFlight = 0;
            if (GatedAsyncWebServer* gs = GatedAsyncWebServer::active()) {
                inFlight = gs->admission().inFlight();
            }
            DBG("HEAP", "publish skip free=%u lg=%u min=%u inflight=%u sse=%u/%u op=%u n=%u/%u low=%u",
                (unsigned)publishFree, (unsigned)heapLargestUsable(),
                (unsigned)heapMinFreeUsable(), (unsigned)inFlight,
                (unsigned)events.count(), (unsigned)events.avgPacketsWaiting(),
                (unsigned)g_runtimeOperationCoordinator.status().operation,
                (unsigned)g_mqttHeapSkips.skips(), (unsigned)g_mqttHeapSkips.suppressed(),
                (unsigned)g_mqttHeapSkips.lowestFree());
        }
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
                    evtDoc["event_id"]    = evt.event_id;
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
                    PublishResult publishResult = mqttService.publishResult(
                        topics.alarm_event, evtJson.c_str(), false, evt.event_id);
                    if (mqttPublishResultConsumes(publishResult)) {
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
                // TIER 1 (state) + TIER 2 (primary sensor) caches were NOT
                // refreshed by the heartbeat, so HA last_updated froze at boot
                // for alarm/presence/distance/motion_type/energy while the node
                // was healthy (see REPORT_ESP_HA_*_CHECK). Invalidate them too so
                // the next TIER 1/TIER 2 pass republishes the current value.
                lastPub.presence_state[0] = '\xFF'; lastPub.presence_state[1] = '\0';
                lastPub.alarm_state[0]    = '\xFF'; lastPub.alarm_state[1]    = '\0';
                lastPub.motion_type[0]    = '\xFF'; lastPub.motion_type[1]    = '\0';
                lastPub.direction[0]      = '\xFF'; lastPub.direction[1]      = '\0';
                lastPub.distance_cm = 0xFFFF;
                lastPub.energy_mov  = 0xFF;
                lastPub.energy_stat = 0xFF;
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

            // ETH PHY link state. The topic was called "rssi" until
            // v5.7.0-dev14 — it never carried a dBm value, and reading the
            // broker suggested the node published RSSI when it never has.
            mqttService.publish(topics.eth_link, ETH.linkUp() ? "ON" : "OFF", true);

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

            uint32_t curHeap = heapFreeUsable() / 1024;
            if (changedU32(curHeap, lastPub.free_heap_kb, DEADBAND_FREE_HEAP_KB)) {
                snprintf(numBuf, sizeof(numBuf), "%u", curHeap);
                if (mqttService.publish(topics.free_heap, numBuf)) {
                    lastPub.free_heap_kb = curHeap;
                }
            }

            uint32_t curMaxAlloc = heapLargestUsable() / 1024;
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
                uint32_t freeHeap = heapFreeUsable();
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
            heapFreeUsable(),
            heapMinFreeUsable(),
            heapLargestUsable(),
            securityMonitor.getAlarmStateStr(),
            mqttService.connected() ? "CONNECTED" : "DISCONNECTED"
        );
    }

    delay(10);
}
