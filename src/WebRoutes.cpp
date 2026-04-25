#ifndef LITE_BUILD
#include "WebRoutes.h"
#include <esp_task_wdt.h>
#include "ConfigManager.h"
#include <ETH.h>

// Radar OUT pin - must match main.cpp definition
#ifndef RADAR_OUT_PIN
#define RADAR_OUT_PIN -1
#endif
#include "services/LD2412Service.h"
#include "services/MQTTService.h"
#include "services/SecurityMonitor.h"
#include "services/TelegramService.h"
#include "services/NotificationService.h"
#include "services/LogService.h"
#include "services/EventLog.h"
#include "services/ConfigSnapshot.h"
#ifdef USE_CSI
#include "services/CSIService.h"
#endif
#include <LittleFS.h>
#include "services/BluetoothService.h"
#include "debug.h"
#include <ArduinoJson.h>
#include <Update.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include "constants.h"
#include "web_interface.h"
#include <esp_ota_ops.h>

// Radar task handle (defined in main.cpp) — suspended during OTA to reduce
// CPU/UART contention with flash writes.
extern TaskHandle_t radarTaskHandle;

namespace WebRoutes {

// Static copy of dependencies - persists after setup() returns
static Dependencies _deps;

// Authentication helper implementation
bool checkAuth(AsyncWebServerRequest *request) {
    if (_deps.config == nullptr) return false;
    if (!request->authenticate(_deps.config->auth_user, _deps.config->auth_pass)) {
        request->requestAuthentication();
        return false;
    }
    return true;
}

void setup(Dependencies& deps) {
    // Copy all pointers to static storage
    _deps = deps;

    DBG("WEB", "Setting up web routes...");

    // Root handler — serve from LittleFS if available, otherwise fall back to PROGMEM
    _deps.server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (LittleFS.exists("/index.html.gz")) {
            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html.gz", "text/html");
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("X-Asset-Source", "littlefs");
            request->send(response);
        } else {
            AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (const uint8_t*)index_html, strlen(index_html));
            response->addHeader("X-Asset-Source", "progmem");
            request->send(response);
        }
    });

    setupTelemetryRoutes();
    setupConfigRoutes();
    setupSecurityRoutes();
    setupSystemRoutes();
    setupAlarmRoutes();
    setupLogRoutes();
    setupSnapshotRoutes();
    setupWwwRoutes();
    setupCSIRoutes();

    DBG("WEB", "Web routes initialized");
}

void setupTelemetryRoutes() {
    _deps.server->on("/api/telemetry", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        _deps.radar->getTelemetryJson(doc);
        doc["hold_time"] = _deps.radar->getHoldTime();
        doc["connected"] = _deps.radar->isRadarConnected();
        #if RADAR_OUT_PIN >= 0
        doc["out_pin"] = digitalRead(RADAR_OUT_PIN);
        #endif
        // Stream directly to TCP instead of building a String first — avoids
        // a duplicated heap allocation on the request path and the transient
        // memory spike that caused curl (52) empty-reply failures under load.
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["uptime"] = millis() / 1000;
        doc["fw_version"] = _deps.fwVersion;
        doc["resolution"] = _deps.config->radar_resolution;
        doc["is_default_pass"] = (String(_deps.config->auth_user) == "admin" && String(_deps.config->auth_pass) == "admin");
        doc["hostname"] = String(_deps.config->hostname);
        doc["reset_history"] = _deps.preferences->getString("reset_history", "[]");

        // Extended Health Stats (REQ-002)
        doc["uart_state"] = _deps.radar->getUARTStateString();
        doc["frame_rate"] = _deps.radar->getFrameRate();
        doc["error_count"] = _deps.radar->getErrorCount();
        doc["health_score"] = _deps.radar->getHealthScore();
        doc["free_heap"] = ESP.getFreeHeap();
        doc["min_heap"] = ESP.getMinFreeHeap();
        doc["chip_temp"] = temperatureRead();

        // OTA rollback state
        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
            doc["ota_state"] = (ota_state == ESP_OTA_IMG_PENDING_VERIFY) ? "pending_verify" :
                               (ota_state == ESP_OTA_IMG_VALID)          ? "valid" :
                               (ota_state == ESP_OTA_IMG_INVALID)        ? "invalid" : "unknown";
        }

        JsonObject build = doc["build"].to<JsonObject>();
        build["rx_pin"]  = RADAR_RX_PIN;
        build["tx_pin"]  = RADAR_TX_PIN;
        build["out_pin"] = RADAR_OUT_PIN;

        JsonObject chip = doc["chip"].to<JsonObject>();
        chip["model"]      = ESP.getChipModel();
        chip["revision"]   = ESP.getChipRevision();
        chip["cores"]      = ESP.getChipCores();
        chip["mac"]        = ETH.macAddress();
        chip["flash_size"] = ESP.getFlashChipSize();

        JsonObject eth = doc["ethernet"].to<JsonObject>();
        eth["link_up"] = ETH.linkUp();
        eth["ip"] = ETH.localIP().toString();
        eth["mac"] = ETH.macAddress();
        eth["speed"] = ETH.linkSpeed();

        JsonObject mqtt = doc["mqtt"].to<JsonObject>();
        mqtt["enabled"] = _deps.config->mqtt_enabled;
        mqtt["connected"] = _deps.mqttService->connected();
        mqtt["server"] = _deps.config->mqtt_server;
        mqtt["port"] = _deps.config->mqtt_port;
        mqtt["user"] = strlen(_deps.config->mqtt_user) > 0 ? "***" : "";
        mqtt["id"] = _deps.config->mqtt_id;
        mqtt["tls"] = (String(_deps.config->mqtt_port) == "8883");

        // Publish health diagnostics (v4.5.6 — detect stuck sessions before DMS)
        unsigned long lastPubMs = _deps.mqttService->getLastPublishTime();
        mqtt["last_publish_age_s"] = lastPubMs > 0 ? (millis() - lastPubMs) / 1000 : 0;
        mqtt["publish_fail_streak"] = _deps.mqttService->getPublishFailStreak();
        mqtt["publish_fail_total"]  = _deps.mqttService->getPublishFailTotal();
        mqtt["reconnect_total"]     = _deps.mqttService->getReconnectTotal();
        mqtt["last_fail_topic"]     = _deps.mqttService->getLastFailTopic();
        mqtt["last_fail_state"]     = _deps.mqttService->getLastFailState();
        mqtt["dms_restart_count"]   = _deps.preferences->getUInt("dms_count", 0);

        // CSI status (compiled flag + runtime active flag)
        JsonObject csi = doc["csi"].to<JsonObject>();
        #ifdef USE_CSI
        csi["compiled"] = true;
        csi["enabled"]  = _deps.config->csi_enabled;
        csi["active"]   = (_deps.csiService != nullptr) && _deps.csiService->isActive();
        if (_deps.csiService != nullptr) {
            csi["threshold"]        = _deps.csiService->getThreshold();
            csi["idle_ready"]       = _deps.csiService->isIdleInitialized();
            csi["auto_cal_enabled"] = _deps.csiService->isAutoCalEnabled();
            csi["auto_cal_done"]    = _deps.csiService->isAutoCalDone();
            csi["auto_cal_quiet_s"] = _deps.csiService->getAutoCalQuietSeconds();
            csi["auto_cal_elapsed"] = _deps.csiService->getAutoCalQuietElapsed();
            // WiFi RSSI diagnostics — surface warning fields for HA operators
            // so they can see when CSI quality is degraded by weak signal.
            int rssi = _deps.csiService->getWifiRSSI();
            csi["wifi_rssi"]     = rssi;
            csi["rssi_low_snr"]  = (rssi != 0 && rssi < CSI_RSSI_WEAK_DBM);
            csi["rssi_too_hot"]  = (rssi != 0 && rssi > CSI_RSSI_HOT_DBM);
            // csi2 stuck-motion state
            csi["stuck_motion_count"] = _deps.csiService->getStuckMotionCount();
            csi["stuck_raise_count"]  = _deps.csiService->getStuckRaiseCount();
            csi["base_threshold"]     = _deps.csiService->getBaseThreshold();
            // csi3 BSSID change diagnostics
            csi["bssid_change_count"] = _deps.csiService->getBssidChangeCount();
            // csi4 adaptive threshold
            csi["adaptive_enabled"]   = _deps.csiService->isAdaptiveThresholdEnabled();
            csi["adaptive_threshold"] = _deps.csiService->getAdaptiveThreshold();
            csi["effective_threshold"]= _deps.csiService->getEffectiveThreshold();
            csi["p95_samples"]        = _deps.csiService->getP95SampleCount();
            // csi5 NBVI subcarrier auto-selection
            csi["nbvi_enabled"]       = _deps.csiService->isNbviEnabled();
            csi["nbvi_ready"]         = _deps.csiService->isNbviReady();
            csi["nbvi_mask"]          = _deps.csiService->getNbviMask();
            csi["nbvi_active"]        = _deps.csiService->getNbviActiveCount();
            csi["nbvi_samples"]       = _deps.csiService->getNbviSamples();
            csi["nbvi_recalc_count"]  = _deps.csiService->getNbviRecalcCount();
            csi["nbvi_best_sc"]       = _deps.csiService->getNbviBestSC();
            csi["nbvi_worst_sc"]      = _deps.csiService->getNbviWorstSC();
            csi["nbvi_best_cv"]       = _deps.csiService->getNbviBestScore();
            csi["nbvi_worst_cv"]      = _deps.csiService->getNbviWorstScore();
            // csi6 site learning
            csi["learning_active"]           = _deps.csiService->isSiteLearning();
            csi["learning_progress"]         = _deps.csiService->getSiteLearningProgress() * 100.0f;
            csi["learning_elapsed_s"]        = _deps.csiService->getSiteLearningElapsedSec();
            csi["learning_duration_s"]       = _deps.csiService->getSiteLearningDurationSec();
            csi["learning_samples"]          = _deps.csiService->getSiteLearningAcceptedSamples();
            csi["learning_rejected_motion"]  = _deps.csiService->getSiteLearningRejectedMotion();
            csi["learning_rejected_radar"]   = _deps.csiService->getSiteLearningRejectedRadar();
            csi["learning_bssid_resets"]     = _deps.csiService->getSiteLearningResetBssid();
            csi["learning_threshold_estimate"] = _deps.csiService->getSiteLearningThresholdEstimate();
            csi["model_ready"]               = _deps.csiService->hasLearnedSiteModel();
            csi["learned_threshold"]         = _deps.csiService->getLearnedThreshold();
            csi["learned_mean_variance"]     = _deps.csiService->getLearnedVarianceMean();
            csi["learned_std_variance"]      = _deps.csiService->getLearnedVarianceStd();
            csi["learned_max_variance"]      = _deps.csiService->getLearnedVarianceMax();
            csi["learned_samples"]           = _deps.csiService->getLearnedSampleCount();
            csi["learn_refresh_count"]       = _deps.csiService->getLearnRefreshCount();
            // csi8 MLP
            csi["ml_enabled"]     = _deps.csiService->isMlEnabled();
            csi["ml_probability"] = _deps.csiService->getMlProbability();
            csi["ml_motion"]      = _deps.csiService->getMlMotionState();
            csi["ml_threshold"]   = _deps.csiService->getMlThreshold();
        }
        #else
        csi["compiled"] = false;
        csi["enabled"]  = false;
        csi["active"]   = false;
        #endif

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/radar/learn-static", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        uint16_t dur = 180;
        if (request->hasParam("duration")) dur = request->getParam("duration")->value().toInt();
        dur = constrain(dur, 30, 28800);
        if (_deps.radar->startStaticLearn(dur)) {
            request->send(200, "text/plain", "Learn started");
        } else {
            request->send(409, "text/plain", "Already running");
        }
    });

    _deps.server->on("/api/radar/learn-static", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        _deps.radar->getLearnResultJson(doc);
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    // Auto-create ignore_static_only zone from learn results
    _deps.server->on("/api/radar/apply-learn", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        // Get learn results
        JsonDocument learnDoc;
        _deps.radar->getLearnResultJson(learnDoc);

        if (!(learnDoc["suggest_ready"] | false)) {
            request->send(409, "text/plain", "No valid learn data — run /learn first");
            return;
        }

        int minCm = learnDoc["suggest_min_cm"] | 0;
        int maxCm = learnDoc["suggest_max_cm"] | 0;
        int topGate = learnDoc["top_gate"] | 0;

        // Optional custom name from query param
        String zoneName = "refl_g" + String(topGate);
        if (request->hasParam("name")) {
            zoneName = request->getParam("name")->value();
            zoneName = zoneName.substring(0, 15); // fit AlertZone.name[16]
        }

        // Parse existing zones
        JsonDocument zonesDoc;
        String currentZones;
        if (_deps.zonesMutex && xSemaphoreTake(*_deps.zonesMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            currentZones = *_deps.zonesJson;
            xSemaphoreGive(*_deps.zonesMutex);
        } else {
            currentZones = "[]";
        }
        deserializeJson(zonesDoc, currentZones);
        JsonArray arr = zonesDoc.to<JsonArray>();

        // Check for overlapping zone
        for (JsonObject z : arr) {
            int zMin = z["min"] | 0;
            int zMax = z["max"] | 0;
            if (minCm <= zMax && maxCm >= zMin) {
                request->send(409, "text/plain",
                    "Overlaps with existing zone '" + String((const char*)(z["name"] | "?")) + "' (" + String(zMin) + "-" + String(zMax) + "cm)");
                return;
            }
        }

        // Append new ignore_static_only zone
        JsonObject newZone = arr.add<JsonObject>();
        newZone["name"] = zoneName;
        newZone["min"] = minCm;
        newZone["max"] = maxCm;
        newZone["level"] = 0;
        newZone["delay"] = 0;
        newZone["enabled"] = true;
        newZone["alarm_behavior"] = 3;  // ignore_static_only

        String newJson;
        serializeJson(zonesDoc, newJson);

        // Schedule zones update (same mechanism as POST /api/zones)
        if (_deps.zonesMutex && xSemaphoreTake(*_deps.zonesMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            *_deps.pendingZonesJson = newJson;
            *_deps.pendingZonesUpdate = true;
            xSemaphoreGive(*_deps.zonesMutex);
        }

        DBG("WEB", "Auto-zone from learn: %s %d-%dcm (behavior=3)", zoneName.c_str(), minCm, maxCm);

        JsonDocument respDoc;
        respDoc["status"] = "created";
        respDoc["zone_name"] = zoneName;
        respDoc["min_cm"] = minCm;
        respDoc["max_cm"] = maxCm;
        respDoc["alarm_behavior"] = 3;
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(respDoc, *response);
        request->send(response);
    });
}

void setupConfigRoutes() {
    // --- Global Config ---
    // NOTE: exact() is required so that /api/config/export and /api/config/import
    // reach their own handlers below. Without it, /api/config's prefix match
    // silently swallows the /export and /import subroutes.
    _deps.server->on(AsyncURIMatcher::exact("/api/config"), HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;

        doc["min_gate"] = _deps.radar->getMinGate();
        doc["max_gate"] = _deps.radar->getMaxGate();
        doc["duration"] = _deps.radar->getMaxGateDuration();
        doc["hold_time"] = _deps.radar->getHoldTime();
        doc["resolution"] = _deps.config->radar_resolution;
        doc["led_start"] = _deps.config->startup_led_sec;
        doc["led_en"] = _deps.config->led_enabled;
        doc["chip_temp_interval"] = _deps.config->chip_temp_interval;
        doc["eng_mode"] = _deps.radar->isEngineeringMode();

        JsonArray mov = doc["mov_sens"].to<JsonArray>();
        JsonArray stat = doc["stat_sens"].to<JsonArray>();

        const uint8_t* m = _deps.radar->getMotionSensitivityArray();
        const uint8_t* s = _deps.radar->getStaticSensitivityArray();

        for(int i=0; i<14; i++) {
            mov.add(m[i]);
            stat.add(s[i]);
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on(AsyncURIMatcher::exact("/api/config"), HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        bool changed = false;
        bool needsReboot = false;

        if (request->hasParam("min_gate") && request->hasParam("gate")) {
             int min_gate = request->getParam("min_gate")->value().toInt();
             int max_gate = request->getParam("gate")->value().toInt();

             if (min_gate >= 0 && min_gate <= 13 && max_gate >= 1 && max_gate <= 13 && min_gate <= max_gate) {
                 _deps.radar->setParamConfig((uint8_t)min_gate, (uint8_t)max_gate, 10);
                 _deps.preferences->putUInt("radar_min", min_gate);
                 _deps.preferences->putUInt("radar_max", max_gate);
                 changed = true;
             }
        }

        if (request->hasParam("hold")) {
            unsigned long hold = request->getParam("hold")->value().toInt();
            if (hold >= 1 && hold <= 300000) {
                _deps.radar->setHoldTime(hold);
                _deps.preferences->putULong("hold_time", hold);
                changed = true;
            }
        }

        if (request->hasParam("mov")) {
            int mov = request->getParam("mov")->value().toInt();
            _deps.radar->setMotionSensitivity((uint8_t)mov);
            changed = true;
        }

        if (request->hasParam("led_en")) {
            bool en = request->getParam("led_en")->value() == "1";
            _deps.config->led_enabled = en;
            _deps.preferences->putBool("led_en", en);
            changed = true;
        }

        if (request->hasParam("chip_temp_interval")) {
            int intv = request->getParam("chip_temp_interval")->value().toInt();
            if (intv >= 0 && intv <= 86400) {
                _deps.config->chip_temp_interval = (uint16_t)intv;
                _deps.preferences->putUShort("temp_intv", (uint16_t)intv);
                changed = true;
            }
        }

        if (request->hasParam("hostname")) {
            String hn = request->getParam("hostname")->value();
            if (hn.length() > 0 && hn.length() < 32) {
                strncpy(_deps.config->hostname, hn.c_str(), sizeof(_deps.config->hostname)-1);
                _deps.preferences->putString("hostname", hn);
                changed = true;
                needsReboot = true;
            }
        }

        if (request->hasParam("resolution")) {
            float res = request->getParam("resolution")->value().toFloat();
            if (res >= 0.19f && res <= 0.21f) res = 0.20f;
            else if (res >= 0.49f && res <= 0.51f) res = 0.50f;
            else if (res >= 0.74f && res <= 0.76f) res = 0.75f;
            if (res == 0.20f || res == 0.50f || res == 0.75f) {
               _deps.config->radar_resolution = res;
               _deps.preferences->putFloat("radar_res", res);
               _deps.radar->setResolution(res);  // Send cmd 0x01 to radar
               changed = true;
               needsReboot = true;
            }
        }

        if (changed) {
            request->send(200, "text/plain", needsReboot ? "Config saved, rebooting..." : "Config saved");
            if (needsReboot) *_deps.shouldReboot = true;
        } else {
            request->send(400, "text/plain", "No valid parameters provided");
        }
    });

    // /api/wifi/config removed — POE board has no WiFi

    // --- MQTT Config ---
    _deps.server->on("/api/mqtt/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        bool en = true;
        if (request->hasParam("enabled")) {
             en = (request->getParam("enabled")->value() == "1");
             _deps.preferences->putBool("mqtt_en", en);
             _deps.config->mqtt_enabled = en;
        }

        if (request->hasParam("server")) {
            String s = request->getParam("server")->value();
            _deps.preferences->putString("mqtt_server", s);
            s.toCharArray(_deps.config->mqtt_server, 60);

            if (request->hasParam("port")) {
                String p = request->getParam("port")->value();
                _deps.preferences->putString("mqtt_port", p);
                p.toCharArray(_deps.config->mqtt_port, 6);
            }
            if (request->hasParam("user")) {
                String u = request->getParam("user")->value();
                if (u != "***") { _deps.preferences->putString("mqtt_user", u); u.toCharArray(_deps.config->mqtt_user, 40); }
            }
            if (request->hasParam("pass")) {
                String pw = request->getParam("pass")->value();
                if (pw != "***") { _deps.preferences->putString("mqtt_pass", pw); pw.toCharArray(_deps.config->mqtt_pass, 40); }
            }

            String id = request->hasParam("id") ? request->getParam("id")->value() : String(_deps.config->mqtt_id);
            _deps.preferences->putString("mqtt_id", id);
            id.toCharArray(_deps.config->mqtt_id, 40);
        }

        request->send(200, "text/plain", "Saved");
        *_deps.shouldReboot = true;
    });

    _deps.server->on("/api/preset", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (request->hasParam("name")) {
            String name = request->getParam("name")->value();
            DBG("WEB", "Applying preset: %s", name.c_str());
            
            if (name == "indoor") {
                _deps.radar->setMotionSensitivity(50);
                _deps.radar->setStaticSensitivity(40);
                _deps.securityMonitor->setPetImmunity(5);
                _deps.preferences->putUInt("sec_pet", 5);
            } 
            else if (name == "outdoor") {
                uint8_t m[14] = {20,30,40,40,40,40,40,40,40,40,40,30,20,10};
                _deps.radar->setMotionSensitivity(m);
                _deps.radar->setStaticSensitivity(20);
                _deps.securityMonitor->setPetImmunity(15);
                _deps.preferences->putUInt("sec_pet", 15);
            }
            else if (name == "pet") {
                uint8_t m[14] = {10,10,15,30,45,50,50,50,50,50,50,50,50,50};
                _deps.radar->setMotionSensitivity(m);
                _deps.radar->setStaticSensitivity(25);
                _deps.securityMonitor->setPetImmunity(25);
                _deps.preferences->putUInt("sec_pet", 25);
            }
            
            request->send(200, "text/plain", "Preset applied");
        } else {
            request->send(400, "text/plain", "Missing name");
        }
    });

    // --- Telegram Config ---
    _deps.server->on("/api/telegram/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["enabled"] = _deps.telegramBot->isEnabled();
        doc["token"] = strlen(_deps.telegramBot->getToken()) > 0 ? "***" : "";
        doc["chat_id"] = strlen(_deps.telegramBot->getChatId()) > 0 ? "***" : "";

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/telegram/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        auto getP = [&](const char* n) -> String {
            if (request->hasParam(n, true)) return request->getParam(n, true)->value();
            if (request->hasParam(n))       return request->getParam(n)->value();
            return "";
        };
        String token = getP("token");
        String chat  = getP("chat_id");
        bool enabled = getP("enabled") == "1";

        if (token.length() > 0 && token != "***") {
            _deps.telegramBot->setToken(token.c_str());
        }
        if (chat.length() > 0 && chat != "***") {
            _deps.telegramBot->setChatId(chat.c_str());
        }
        _deps.notificationService->setTelegramConfig(
            _deps.telegramBot->getToken(), _deps.telegramBot->getChatId());
        _deps.telegramBot->setEnabled(enabled);

        request->send(200, "text/plain", "Telegram config saved");
        if (enabled) *_deps.shouldReboot = true;
    });

    _deps.server->on("/api/telegram/test", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        String testMsg = "🔔 *Test Notifikace*\n";
        testMsg += "📡 " + String(_deps.config->mqtt_id) + "\n";
        testMsg += "🌐 " + ETH.localIP().toString() + "\n";
        testMsg += "🏷️ FW: " + String(_deps.fwVersion);
        bool res = _deps.telegramBot->sendMessageDirect(testMsg);
        JsonDocument resDoc;
        resDoc["success"] = res;
        if (!res) resDoc["error"] = "Send failed (DNS/TCP/TLS/token/chat_id)";
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(resDoc, *response);
        request->send(response);
    });

    // --- Auth Config ---
    _deps.server->on("/api/auth/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (request->hasParam("user") && request->hasParam("pass")) {
            String user = request->getParam("user")->value();
            String pass = request->getParam("pass")->value();
            if (user.length() >= 4 && pass.length() >= 4) {
                strncpy(_deps.config->auth_user, user.c_str(), sizeof(_deps.config->auth_user)-1);
                strncpy(_deps.config->auth_pass, pass.c_str(), sizeof(_deps.config->auth_pass)-1);
                _deps.preferences->putString("auth_user", user);
                _deps.preferences->putString("auth_pass", pass);
                request->send(200, "text/plain", "Credentials changed, rebooting...");
                *_deps.shouldReboot = true;
            } else {
                request->send(400, "text/plain", "Too short (min 4 chars)");
            }
        } else {
            request->send(400, "text/plain", "Missing params");
        }
    });

    // --- Zones ---
    _deps.server->on("/api/zones", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        String copy;
        if (_deps.zonesMutex && xSemaphoreTake(*_deps.zonesMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            copy = *_deps.zonesJson;
            xSemaphoreGive(*_deps.zonesMutex);
        } else {
            copy = "[]";
        }
        request->send(200, "application/json", copy);
    });

    _deps.server->on("/api/zones", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        char* bodyData = (char*)request->_tempObject;
        if (bodyData) {
            if (strlen(bodyData) > 0) {
                if (_deps.zonesMutex && xSemaphoreTake(*_deps.zonesMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    *_deps.pendingZonesJson = String(bodyData);
                    *_deps.pendingZonesUpdate = true;
                    xSemaphoreGive(*_deps.zonesMutex);
                }
            }
            free(bodyData);
            request->_tempObject = nullptr;
        }

        request->send(200, "text/plain", "Zones received");
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkAuth(request)) return;

        if (index == 0) {
            if (total > 4096) return;  // Limit body size
            request->_tempObject = malloc(total + 1);
            if (request->_tempObject) ((char*)request->_tempObject)[0] = '\0';
        }

        char* buf = (char*)request->_tempObject;
        if (buf && index + len <= total) {
            memcpy(buf + index, data, len);
            buf[index + len] = '\0';
        }
    });

    // --- Config Export/Import ---
    _deps.server->on("/api/config/export", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["mqtt_server"] = String(_deps.config->mqtt_server);
        doc["mqtt_port"] = String(_deps.config->mqtt_port);
        doc["mqtt_user"] = strlen(_deps.config->mqtt_user) > 0 ? "***" : "";
        doc["mqtt_pass"] = strlen(_deps.config->mqtt_pass) > 0 ? "***" : "";
        doc["mqtt_id"] = String(_deps.config->mqtt_id);
        doc["mqtt_en"] = _deps.config->mqtt_enabled;
        doc["hostname"] = String(_deps.config->hostname);
        doc["auth_user"] = "***";
        doc["auth_pass"] = "***";
        doc["radar_res"] = _deps.config->radar_resolution;
        doc["radar_min"] = _deps.radar->getMinGate();
        doc["radar_max"] = _deps.radar->getMaxGate();
        doc["led_en"] = _deps.config->led_enabled;
        doc["led_start"] = _deps.config->startup_led_sec;
        doc["hold_time"] = _deps.radar->getHoldTime();
        doc["pet_immunity"] = _deps.radar->getMinMoveEnergy();
        doc["chip_temp_interval"] = _deps.config->chip_temp_interval;
        doc["radar_bt"] = _deps.config->radar_bluetooth;

        // Telegram (token redacted)
        doc["tg_enabled"] = _deps.telegramBot ? _deps.telegramBot->isEnabled() : false;
        doc["tg_token"] = (_deps.telegramBot && strlen(_deps.telegramBot->getToken()) > 0) ? "***" : "";
        doc["tg_chat"] = (_deps.telegramBot && strlen(_deps.telegramBot->getChatId()) > 0)
            ? String(_deps.telegramBot->getChatId()) : String("");

        // Timezone
        doc["tz_offset"] = _deps.config->tz_offset;
        doc["dst_offset"] = _deps.config->dst_offset;

        // Static network (empty => DHCP)
        doc["static_ip"] = String(_deps.config->static_ip);
        doc["static_mask"] = String(_deps.config->static_mask);
        doc["static_gw"] = String(_deps.config->static_gw);
        doc["static_dns"] = String(_deps.config->static_dns);

        // Schedule
        doc["sched_arm"] = String(_deps.config->sched_arm_time);
        doc["sched_disarm"] = String(_deps.config->sched_disarm_time);
        doc["auto_arm_min"] = _deps.config->auto_arm_minutes;

        // Security (read back from NVS — reflects persisted state)
        doc["sec_antimask"] = _deps.preferences->getULong("sec_antimask", 0);
        doc["sec_am_en"] = _deps.preferences->getBool("sec_am_en", false);
        doc["sec_loiter"] = _deps.preferences->getULong("sec_loiter", 0);
        doc["sec_loit_en"] = _deps.preferences->getBool("sec_loit_en", true);
        doc["sec_hb"] = _deps.preferences->getULong("sec_hb", 0);
        doc["sec_pet"] = _deps.preferences->getUInt("sec_pet", 0);
        doc["sec_entry_dl"] = _deps.preferences->getULong("sec_entry_dl", 0);
        doc["sec_exit_dl"] = _deps.preferences->getULong("sec_exit_dl", 0);
        doc["sec_dis_rem"] = _deps.preferences->getBool("sec_dis_rem", false);

        // CSI runtime config
        doc["csi_en"] = _deps.config->csi_enabled;
        doc["csi_thr"] = _deps.config->csi_threshold;
        doc["csi_hyst"] = _deps.config->csi_hysteresis;
        doc["csi_win"] = _deps.config->csi_window;
        doc["csi_pubms"] = _deps.config->csi_publish_ms;
        doc["csi_ssid"] = String(_deps.config->csi_ssid);
        doc["csi_pass"] = strlen(_deps.config->csi_pass) > 0 ? "***" : "";
        doc["fus_en"] = _deps.config->fusion_enabled;
        // CSI traffic gen + ML (NVS-only, no struct mirror)
        doc["csi_tport"] = _deps.preferences->getUShort("csi_tport", 7);
        doc["csi_ticmp"] = _deps.preferences->getBool("csi_ticmp", false);
        doc["csi_tpps"] = _deps.preferences->getUInt("csi_tpps", 100);
        doc["csi_ml_en"] = _deps.preferences->getBool("csi_ml_en", true);
        doc["csi_ml_thr"] = _deps.preferences->getFloat("csi_ml_thr", 0.50f);

        if (_deps.zonesMutex && xSemaphoreTake(*_deps.zonesMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            doc["zones"] = *_deps.zonesJson;
            xSemaphoreGive(*_deps.zonesMutex);
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/config/import", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        char* importData = (char*)request->_tempObject;
        if (importData) {
            if (strlen(importData) > 0) {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, importData);
                if (err) {
                    free(importData);
                    request->_tempObject = nullptr;
                    request->send(400, "text/plain", "Invalid JSON");
                    return;
                }

                if (doc["mqtt_server"].is<String>()) _deps.preferences->putString("mqtt_server", doc["mqtt_server"].as<String>());
                if (doc["mqtt_port"].is<String>()) _deps.preferences->putString("mqtt_port", doc["mqtt_port"].as<String>());
                if (doc["mqtt_user"].is<String>() && doc["mqtt_user"].as<String>() != "***") _deps.preferences->putString("mqtt_user", doc["mqtt_user"].as<String>());
                if (doc["mqtt_pass"].is<String>() && doc["mqtt_pass"].as<String>() != "***") _deps.preferences->putString("mqtt_pass", doc["mqtt_pass"].as<String>());
                if (doc["mqtt_id"].is<String>()) _deps.preferences->putString("mqtt_id", doc["mqtt_id"].as<String>());
                if (doc["mqtt_en"].is<bool>()) _deps.preferences->putBool("mqtt_en", doc["mqtt_en"].as<bool>());
                if (doc["hostname"].is<String>()) _deps.preferences->putString("hostname", doc["hostname"].as<String>());
                if (doc["auth_user"].is<String>() && doc["auth_user"].as<String>() != "***") _deps.preferences->putString("auth_user", doc["auth_user"].as<String>());
                if (doc["auth_pass"].is<String>() && doc["auth_pass"].as<String>() != "***") _deps.preferences->putString("auth_pass", doc["auth_pass"].as<String>());
                // bk_ssid/bk_pass removed — POE board has no WiFi
                if (doc["radar_res"].is<float>()) _deps.preferences->putFloat("radar_res", doc["radar_res"].as<float>());
                if (doc["radar_min"].is<unsigned int>()) _deps.preferences->putUInt("radar_min", doc["radar_min"].as<unsigned int>());
                if (doc["radar_max"].is<unsigned int>()) _deps.preferences->putUInt("radar_max", doc["radar_max"].as<unsigned int>());
                if (doc["led_en"].is<bool>()) _deps.preferences->putBool("led_en", doc["led_en"].as<bool>());
                if (doc["led_start"].is<uint16_t>()) _deps.preferences->putUInt("led_start", doc["led_start"].as<uint16_t>());
                if (doc["hold_time"].is<unsigned long>()) _deps.preferences->putULong("hold_time", doc["hold_time"].as<unsigned long>());
                if (doc["chip_temp_interval"].is<uint16_t>()) _deps.preferences->putUShort("temp_intv", doc["chip_temp_interval"].as<uint16_t>());
                if (doc["radar_bt"].is<bool>()) _deps.preferences->putBool("radar_bt", doc["radar_bt"].as<bool>());

                // Telegram — skip redacted token
                if (doc["tg_enabled"].is<bool>()) _deps.preferences->putBool("tg_direct_en", doc["tg_enabled"].as<bool>());
                if (doc["tg_token"].is<String>() && doc["tg_token"].as<String>() != "***") _deps.preferences->putString("tg_token", doc["tg_token"].as<String>());
                if (doc["tg_chat"].is<String>()) _deps.preferences->putString("tg_chat", doc["tg_chat"].as<String>());

                // Timezone
                if (doc["tz_offset"].is<int32_t>()) _deps.preferences->putInt("tz_offset", doc["tz_offset"].as<int32_t>());
                if (doc["dst_offset"].is<int32_t>()) _deps.preferences->putInt("dst_offset", doc["dst_offset"].as<int32_t>());

                // Static network
                if (doc["static_ip"].is<String>()) _deps.preferences->putString("static_ip", doc["static_ip"].as<String>());
                if (doc["static_mask"].is<String>()) _deps.preferences->putString("static_mask", doc["static_mask"].as<String>());
                if (doc["static_gw"].is<String>()) _deps.preferences->putString("static_gw", doc["static_gw"].as<String>());
                if (doc["static_dns"].is<String>()) _deps.preferences->putString("static_dns", doc["static_dns"].as<String>());

                // Schedule
                if (doc["sched_arm"].is<String>()) _deps.preferences->putString("sched_arm", doc["sched_arm"].as<String>());
                if (doc["sched_disarm"].is<String>()) _deps.preferences->putString("sched_disarm", doc["sched_disarm"].as<String>());
                if (doc["auto_arm_min"].is<uint16_t>()) _deps.preferences->putUShort("auto_arm_min", doc["auto_arm_min"].as<uint16_t>());

                // Security
                if (doc["sec_antimask"].is<unsigned long>()) _deps.preferences->putULong("sec_antimask", doc["sec_antimask"].as<unsigned long>());
                if (doc["sec_am_en"].is<bool>()) _deps.preferences->putBool("sec_am_en", doc["sec_am_en"].as<bool>());
                if (doc["sec_loiter"].is<unsigned long>()) _deps.preferences->putULong("sec_loiter", doc["sec_loiter"].as<unsigned long>());
                if (doc["sec_loit_en"].is<bool>()) _deps.preferences->putBool("sec_loit_en", doc["sec_loit_en"].as<bool>());
                if (doc["sec_hb"].is<unsigned long>()) _deps.preferences->putULong("sec_hb", doc["sec_hb"].as<unsigned long>());
                if (doc["sec_pet"].is<unsigned int>()) _deps.preferences->putUInt("sec_pet", doc["sec_pet"].as<unsigned int>());
                if (doc["sec_entry_dl"].is<unsigned long>()) _deps.preferences->putULong("sec_entry_dl", doc["sec_entry_dl"].as<unsigned long>());
                if (doc["sec_exit_dl"].is<unsigned long>()) _deps.preferences->putULong("sec_exit_dl", doc["sec_exit_dl"].as<unsigned long>());
                if (doc["sec_dis_rem"].is<bool>()) _deps.preferences->putBool("sec_dis_rem", doc["sec_dis_rem"].as<bool>());

                // CSI runtime
                if (doc["csi_en"].is<bool>()) _deps.preferences->putBool("csi_en", doc["csi_en"].as<bool>());
                if (doc["csi_thr"].is<float>()) _deps.preferences->putFloat("csi_thr", doc["csi_thr"].as<float>());
                if (doc["csi_hyst"].is<float>()) _deps.preferences->putFloat("csi_hyst", doc["csi_hyst"].as<float>());
                if (doc["csi_win"].is<uint16_t>()) _deps.preferences->putUShort("csi_win", doc["csi_win"].as<uint16_t>());
                if (doc["csi_pubms"].is<uint16_t>()) _deps.preferences->putUShort("csi_pubms", doc["csi_pubms"].as<uint16_t>());
                if (doc["csi_ssid"].is<String>()) _deps.preferences->putString("csi_ssid", doc["csi_ssid"].as<String>());
                if (doc["csi_pass"].is<String>() && doc["csi_pass"].as<String>() != "***") _deps.preferences->putString("csi_pass", doc["csi_pass"].as<String>());
                if (doc["fus_en"].is<bool>()) _deps.preferences->putBool("fus_en", doc["fus_en"].as<bool>());
                if (doc["csi_tport"].is<uint16_t>()) _deps.preferences->putUShort("csi_tport", doc["csi_tport"].as<uint16_t>());
                if (doc["csi_ticmp"].is<bool>()) _deps.preferences->putBool("csi_ticmp", doc["csi_ticmp"].as<bool>());
                if (doc["csi_tpps"].is<unsigned int>()) _deps.preferences->putUInt("csi_tpps", doc["csi_tpps"].as<unsigned int>());
                if (doc["csi_ml_en"].is<bool>()) _deps.preferences->putBool("csi_ml_en", doc["csi_ml_en"].as<bool>());
                if (doc["csi_ml_thr"].is<float>()) _deps.preferences->putFloat("csi_ml_thr", doc["csi_ml_thr"].as<float>());

                if (doc["zones"].is<String>()) _deps.preferences->putString("zones_json", doc["zones"].as<String>());
            }

            free(importData);
            request->_tempObject = nullptr;
        }

        request->send(200, "text/plain", "Config received, applying and rebooting...");
        *_deps.shouldReboot = true;
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        if (!checkAuth(request)) return;

        if (index == 0) {
            if (total > 4096) return;  // Limit body size
            request->_tempObject = malloc(total + 1);
            if (request->_tempObject) ((char*)request->_tempObject)[0] = '\0';
        }

        char* buf = (char*)request->_tempObject;
        if (buf && index + len <= total) {
            memcpy(buf + index, data, len);
            buf[index + len] = '\0';
        }
    });
}

void setupSecurityRoutes() {
    _deps.server->on("/api/security/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["antimask_time"] = _deps.securityMonitor->getAntiMaskTime() / 1000;
        doc["antimask_enabled"] = _deps.securityMonitor->isAntiMaskEnabled();
        doc["loiter_time"] = _deps.securityMonitor->getLoiterTime() / 1000;
        doc["loiter_alert"] = _deps.securityMonitor->isLoiterAlertEnabled();
        doc["heartbeat"] = _deps.securityMonitor->getHeartbeatInterval() / INTERVAL_TELEMETRY_IDLE_MS;
        doc["pet_immunity"] = _deps.radar->getMinMoveEnergy();
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/security/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        if (request->hasParam("antimask")) {
            unsigned long val = request->getParam("antimask")->value().toInt() * 1000;
            _deps.securityMonitor->setAntiMaskTime(val);
            _deps.preferences->putULong("sec_antimask", val);
        }
        if (request->hasParam("antimask_en")) {
            bool en = (request->getParam("antimask_en")->value() == "1");
            _deps.securityMonitor->setAntiMaskEnabled(en);
            _deps.preferences->putBool("sec_am_en", en);
        }
        if (request->hasParam("loiter")) {
            unsigned long val = request->getParam("loiter")->value().toInt() * 1000;
            _deps.securityMonitor->setLoiterTime(val);
            _deps.preferences->putULong("sec_loiter", val);
        }
        if (request->hasParam("loiter_alert")) {
            bool en = (request->getParam("loiter_alert")->value() == "1");
            _deps.securityMonitor->setLoiterAlertEnabled(en);
            _deps.preferences->putBool("sec_loit_en", en);
        }
        if (request->hasParam("heartbeat")) {
            unsigned long val = request->getParam("heartbeat")->value().toInt() * INTERVAL_TELEMETRY_IDLE_MS;
            _deps.securityMonitor->setHeartbeatInterval(val);
            _deps.preferences->putULong("sec_hb", val);
        }
        if (request->hasParam("pet")) {
            int val = request->getParam("pet")->value().toInt();
            if(val >= 0 && val <= 100) {
                 _deps.radar->setMinMoveEnergy((uint8_t)val);
                 _deps.securityMonitor->setPetImmunity((uint8_t)val);
                 _deps.preferences->putUInt("sec_pet", (uint8_t)val);
            }
        }
        request->send(200, "text/plain", "Security config saved");
    });
}

void setupSystemRoutes() {
    _deps.server->on("/api/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool success = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", success ? "OK" : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response);
        if (success) {
            DBG("OTA", "Update finished, delaying 500ms for response flush before reboot...");
            delay(500);  // Let HTTP response pass through nginx proxy before reboot
            *_deps.shouldReboot = true;
        }
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        // Basic auth only (not Digest) — Digest keeps nonce/body-hash state
        // that AsyncTCP on LAN8720A loses under backpressure, causing the
        // classic 64 KB stall. Basic is stateless.
        static bool otaAuthorized = false;
        if (!index) {
            otaAuthorized = false;
            if (!request->authenticate(_deps.config->auth_user, _deps.config->auth_pass)) {
                DBG("OTA", "Upload auth failed");
                request->requestAuthentication(nullptr, false);  // false = Basic
                return;
            }
            otaAuthorized = true;
            DBG("OTA", "Update start: %s (%u bytes)", filename.c_str(), request->contentLength());
            // freeze CSI WiFi/lwIP interactions so they can't drop the
            // in-flight upload TCP (see CSIService::setOtaInProgress comment).
            CSIService::setOtaInProgress(true);
            // same treatment for MQTT. PubSubClient's blocking
            // WiFiClient.connect() on a failing reconnect was causing
            // non-deterministic upload stalls (42-90% completion).
            MQTTService::setOtaInProgress(true);
            // Reduce runtime load so AsyncTCP receive buffer can keep draining
            // while Update.write() blocks on flash sector writes (~10–20 ms each).
            if (radarTaskHandle) vTaskSuspend(radarTaskHandle);
            if (_deps.configSnapshot && _deps.preferences) {
                _deps.configSnapshot->saveSnapshot(_deps.preferences, _deps.fwVersion, "ota_http");
            }
            size_t updateSize = (request->contentLength() > 0) ? request->contentLength() : UPDATE_SIZE_UNKNOWN;
            if (!Update.begin(updateSize)) {
                Update.printError(Serial);
            }
        }
        if (!otaAuthorized) return;
        if (!Update.hasError()) {
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            esp_task_wdt_reset();
            // Yield so AsyncTCP task can ACK and drain socket buffer. Without
            // this, flash writes monopolise the handler and the 64 KB receive
            // buffer fills before the next chunk can land → connection stall.
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (final) {
            if (!Update.hasError() && Update.end(true)) {
                DBG("OTA", "Update success: %u B — reboot scheduled", index + len);
                *_deps.shouldReboot = true;
            } else {
                Update.printError(Serial);
                Update.abort();
                if (radarTaskHandle) vTaskResume(radarTaskHandle);
                CSIService::setOtaInProgress(false);
                MQTTService::setOtaInProgress(false);
            }
            otaAuthorized = false;
        }
    });

    // Pull-based OTA — device fetches binary from a URL instead of receiving
    // it over HTTP POST. Bypasses AsyncTCP backpressure (no 64 KB stall).
    //
    // Fixes vs. v4.5.10-dev-pull2:
    // - URL-scheme-aware client: WiFiClient for http://, WiFiClientSecure for
    //   https://. Previous version always used WiFiClientSecure, so plain-HTTP
    //   URLs (e.g. LAN python -m http.server) silently failed TLS handshake.
    // - Task stack raised to 16 KB. HTTPUpdate + HTTPClient + TLS buffers
    //   overflowed the old 8 KB stack on HTTPS pulls.
    // - Persistent status written to NVS at every phase, queryable via
    //   GET /api/update/pull/status — survives reboots and failures.
    // - Radar task suspend DEFERRED until http.begin() succeeds, so early
    //   connect failures don't starve the device of radar data.
    //
    // Request:  POST /api/update/pull
    //   Body (JSON): {"url":"...","auth":"Bearer <token>"}  (auth optional)
    // Response: 200 (accepted) | 400 (bad body) | 409 (already in flight)
    static volatile bool otaPullInFlight = false;

    _deps.server->on("/api/update/pull", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (otaPullInFlight) {
            request->send(409, "text/plain", "Pull already in progress");
            return;
        }
        if (!request->_tempObject) {
            request->send(400, "text/plain", "Body missing or too large");
            return;
        }

        JsonDocument doc;
        DeserializationError jerr = deserializeJson(doc, (const char*)request->_tempObject);
        if (jerr) {
            request->send(400, "text/plain", String("Invalid JSON: ") + jerr.c_str());
            return;
        }
        const char* url  = doc["url"]  | "";
        const char* auth = doc["auth"] | "";
        if (strlen(url) == 0) {
            request->send(400, "text/plain", "Missing 'url' field");
            return;
        }

        struct PullParams { String url; String auth; };
        PullParams* p = new(std::nothrow) PullParams{ String(url), String(auth) };
        if (!p) {
            request->send(500, "text/plain", "Out of memory");
            return;
        }

        otaPullInFlight = true;
        DBG("OTA-PULL", "Scheduling pull from '%s' (auth=%s)", p->url.c_str(),
            p->auth.length() > 0 ? "yes" : "no");
        if (_deps.systemLog) _deps.systemLog->info(String("OTA pull scheduled: ") + p->url);

        if (_deps.preferences) {
            _deps.preferences->putString("ota_phase", "accepted");
            _deps.preferences->putString("ota_msg", "");
            _deps.preferences->putInt("ota_err", 0);
            _deps.preferences->putString("ota_url", p->url);
            _deps.preferences->putUInt("ota_ts", (uint32_t)(millis() / 1000));
        }
        if (_deps.configSnapshot && _deps.preferences) {
            _deps.configSnapshot->saveSnapshot(_deps.preferences, _deps.fwVersion, "ota_pull");
        }

        request->send(200, "text/plain",
            "Pull accepted. Watch /api/update/pull/status for progress. "
            "Device reboots only on successful image swap.");

        xTaskCreatePinnedToCore([](void* arg) {
            PullParams* p = static_cast<PullParams*>(arg);
            Preferences* prefs = _deps.preferences;

            auto setPhase = [prefs](const char* phase, const char* msg = "", int err = 0) {
                if (!prefs) return;
                prefs->putString("ota_phase", phase);
                prefs->putString("ota_msg", msg);
                prefs->putInt("ota_err", err);
            };

            // FIX: choose client by URL scheme (previously always WiFiClientSecure,
            // which failed on plain-HTTP URLs with a silent TLS-handshake stall).
            bool isHttps = p->url.startsWith("https://");
            WiFiClient*       plain = nullptr;
            WiFiClientSecure* secure = nullptr;
            WiFiClient*       client = nullptr;
            if (isHttps) {
                secure = new(std::nothrow) WiFiClientSecure();
                if (secure) {
                    secure->setInsecure();
                    secure->setTimeout(30000);
                    client = secure;
                }
            } else {
                plain = new(std::nothrow) WiFiClient();
                if (plain) {
                    plain->setTimeout(30);  // seconds on plain WiFiClient (API diff)
                    client = plain;
                }
            }
            if (!client) {
                DBG("OTA-PULL", "client alloc failed");
                setPhase("failed", "client alloc failed", -1);
                delete p;
                otaPullInFlight = false;
                vTaskDelete(NULL);
                return;
            }
            setPhase("connecting");

            HTTPClient http;
            http.setReuse(false);
            http.setTimeout(30000);
            http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

            bool ok = http.begin(*client, p->url);
            if (!ok) {
                DBG("OTA-PULL", "http.begin() failed for URL");
                setPhase("failed", "http.begin() refused URL", -2);
                delete client;
                delete p;
                otaPullInFlight = false;
                vTaskDelete(NULL);
                return;
            }
            if (p->auth.length() > 0) http.addHeader("Authorization", p->auth);
            http.addHeader("Accept", "application/octet-stream");
            http.addHeader("User-Agent", "poe2412-ota-pull");

            // Now that we're about to do the heavy flash work, free radar CPU
            // and freeze CSI WiFi/lwIP manipulation for the duration of the pull.
            if (radarTaskHandle) vTaskSuspend(radarTaskHandle);
            CSIService::setOtaInProgress(true);
            MQTTService::setOtaInProgress(true);
            setPhase("fetching");

            httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
            httpUpdate.rebootOnUpdate(false);  // we'll reboot explicitly, after status is persisted
            t_httpUpdate_return ret = httpUpdate.update(http);

            bool doReboot = false;
            switch (ret) {
                case HTTP_UPDATE_OK:
                    DBG("OTA-PULL", "Update OK — rebooting");
                    setPhase("success_rebooting", "Update applied, rebooting", 0);
                    doReboot = true;
                    break;
                case HTTP_UPDATE_NO_UPDATES:
                    DBG("OTA-PULL", "No update available");
                    setPhase("no_update", "Server returned no-update", 0);
                    break;
                case HTTP_UPDATE_FAILED:
                default: {
                    int errCode = httpUpdate.getLastError();
                    String errStr = httpUpdate.getLastErrorString();
                    DBG("OTA-PULL", "FAILED: err=%d '%s'", errCode, errStr.c_str());
                    setPhase("failed", errStr.c_str(), errCode);
                    break;
                }
            }
            http.end();
            delete client;
            delete p;

            if (radarTaskHandle) vTaskResume(radarTaskHandle);
            CSIService::setOtaInProgress(false);
            MQTTService::setOtaInProgress(false);
            otaPullInFlight = false;

            if (doReboot) {
                vTaskDelay(pdMS_TO_TICKS(500));  // let response flush, NVS sync
                ESP.restart();
            }
            vTaskDelete(NULL);
        }, "ota_pull", 16384, p, 5, nullptr, 1);  // 16 KB stack for HTTPS+HTTPUpdate
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0) {
            if (total > 2048) return;
            request->_tempObject = malloc(total + 1);
            if (!request->_tempObject) return;
            ((char*)request->_tempObject)[total] = '\0';
        }
        if (!request->_tempObject) return;
        memcpy((char*)request->_tempObject + index, data, len);
    });

    // Query last OTA pull status. Survives reboots via NVS.
    // Phases: "idle" (never pulled), "accepted", "connecting", "fetching",
    //         "success_rebooting", "no_update", "failed"
    _deps.server->on("/api/update/pull/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        Preferences* prefs = _deps.preferences;
        doc["phase"]    = prefs ? prefs->getString("ota_phase", "idle")   : "unknown";
        doc["message"]  = prefs ? prefs->getString("ota_msg",   "")       : "";
        doc["last_err"] = prefs ? prefs->getInt   ("ota_err",   0)        : 0;
        doc["url"]      = prefs ? prefs->getString("ota_url",   "")       : "";
        doc["ts_s"]     = prefs ? prefs->getUInt  ("ota_ts",    0)        : 0;
        doc["in_flight"]= otaPullInFlight;
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    // Debug log ring buffer — remote access to last 4KB of DBG() output
    _deps.server->on("/api/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        request->send(200, "text/plain", DebugLog::instance().read());
    });
    _deps.server->on("/api/debug", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        DebugLog::instance().clear();
        request->send(200, "text/plain", "cleared");
    });

    _deps.server->on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        request->send(200, "text/plain", "Rebooting...");
        *_deps.shouldReboot = true;
    });

    _deps.server->on("/api/radar/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        _deps.radar->restartRadar();
        request->send(200, "text/plain", "Radar restart command sent");
    });

    _deps.server->on("/api/radar/factory_reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (_deps.radar->factoryReset()) {
            request->send(200, "text/plain", "Radar factory reset OK");
        } else {
            request->send(500, "text/plain", "Radar factory reset failed");
        }
    });

    _deps.server->on("/api/radar/calibrate", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        _deps.radar->startCalibration();
        request->send(200, "text/plain", "Calibration started (60s)");
    });

    _deps.server->on("/api/engineering", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (request->hasParam("enable")) {
            bool en = request->getParam("enable")->value() == "1";
            _deps.radar->setEngineeringMode(en);
            request->send(200, "text/plain", en ? "Engineering mode ON" : "Engineering mode OFF");
        } else {
            request->send(400, "text/plain", "Missing enable param");
        }
    });

    // Per-gate sensitivity endpoint (CR-005)
    _deps.server->on("/api/radar/gate", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        if (request->hasParam("gate") && request->hasParam("mov") && request->hasParam("stat")) {
            int gate = request->getParam("gate")->value().toInt();
            int mov = request->getParam("mov")->value().toInt();
            int stat = request->getParam("stat")->value().toInt();

            if (gate >= 0 && gate <= 13 && mov >= 0 && mov <= 100 && stat >= 0 && stat <= 100) {
                // Get current values and modify just one gate
                const uint8_t* currentMov = _deps.radar->getMotionSensitivityArray();
                const uint8_t* currentStat = _deps.radar->getStaticSensitivityArray();

                uint8_t movArr[14], statArr[14];
                memcpy(movArr, currentMov, 14);
                memcpy(statArr, currentStat, 14);

                movArr[gate] = (uint8_t)mov;
                statArr[gate] = (uint8_t)stat;

                bool movOk = _deps.radar->setMotionSensitivity(movArr);
                bool statOk = _deps.radar->setStaticSensitivity(statArr);

                if (movOk && statOk) {
                    request->send(200, "text/plain", "Gate sensitivity updated");
                } else {
                    request->send(500, "text/plain", "Radar command failed");
                }
            } else {
                request->send(400, "text/plain", "Invalid values (gate 0-13, sens 0-100)");
            }
        } else {
            request->send(400, "text/plain", "Missing params: gate, mov, stat");
        }
    });

    // Batch gate sensitivity endpoint — single JSON POST for all 14 gates
    _deps.server->on("/api/radar/gates", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        char* bodyData = (char*)request->_tempObject;
        if (!bodyData || strlen(bodyData) == 0) {
            request->send(400, "text/plain", "Empty body");
            return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, bodyData);
        free(bodyData);
        request->_tempObject = nullptr;

        if (err) {
            request->send(400, "text/plain", "Invalid JSON");
            return;
        }

        JsonArray movArr = doc["mov"];
        JsonArray statArr = doc["stat"];

        if (movArr.size() != 14 || statArr.size() != 14) {
            request->send(400, "text/plain", "Need mov[14] and stat[14]");
            return;
        }

        uint8_t mov[14], stat[14];
        for (int i = 0; i < 14; i++) {
            int m = movArr[i].as<int>();
            int s = statArr[i].as<int>();
            if (m < 0 || m > 100 || s < 0 || s > 100) {
                request->send(400, "text/plain", "Values must be 0-100");
                return;
            }
            mov[i] = (uint8_t)m;
            stat[i] = (uint8_t)s;
        }

        bool movOk = _deps.radar->setMotionSensitivity(mov);
        bool statOk = _deps.radar->setStaticSensitivity(stat);

        if (movOk && statOk) {
            request->send(200, "text/plain", "Gates saved");
        } else {
            request->send(500, "text/plain", "Radar command failed");
        }
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkAuth(request)) return;

        if (index == 0) {
            if (total > 512) return;
            request->_tempObject = malloc(total + 1);
            if (request->_tempObject) ((char*)request->_tempObject)[0] = '\0';
        }

        char* buf = (char*)request->_tempObject;
        if (buf && index + len <= total) {
            memcpy(buf + index, data, len);
            buf[index + len] = '\0';
        }
    });

    _deps.server->on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request) {
        // No auth for version (GUI needs it for the header before auth)
        // Or keep it simple if GUI already handles auth
        request->send(200, "text/plain", _deps.fwVersion);
    });

    // Query Resolution endpoint (Task #12)
    _deps.server->on("/api/radar/resolution", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        int mode = _deps.radar->getResolution();
        doc["mode"] = mode;
        // Convert to float for display
        float res = 0.75f;
        if (mode == 1) res = 0.50f;
        else if (mode == 2) res = 0.20f;
        doc["resolution"] = res;
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    // Light Function/Threshold endpoints (Task #11)
    _deps.server->on("/api/radar/light", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["function"] = _deps.radar->getLightFunction();
        doc["threshold"] = _deps.radar->getLightThreshold();
        doc["current_level"] = _deps.radar->getLightLevel();
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/radar/light", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool changed = false;

        if (request->hasParam("function")) {
            int func = request->getParam("function")->value().toInt();
            if (func >= 0 && func <= 2) {
                if (_deps.radar->setLightFunction((uint8_t)func)) {
                    _deps.preferences->putUChar("light_func", (uint8_t)func);
                    changed = true;
                }
            }
        }
        if (request->hasParam("threshold")) {
            int thresh = request->getParam("threshold")->value().toInt();
            if (thresh >= 0 && thresh <= 255) {
                if (_deps.radar->setLightThreshold((uint8_t)thresh)) {
                    _deps.preferences->putUChar("light_thresh", (uint8_t)thresh);
                    changed = true;
                }
            }
        }

        if (changed) {
            request->send(200, "text/plain", "Light config saved");
        } else {
            request->send(400, "text/plain", "Invalid or missing params");
        }
    });

    // Presence Timeout (Unmanned Duration) endpoint (Task #13)
    _deps.server->on("/api/radar/timeout", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["duration"] = _deps.radar->getMaxGateDuration();
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/radar/timeout", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        // Accept "duration" from either query string OR form body.
        // GUI posts application/x-www-form-urlencoded; external callers may use
        // either. Without the `true` second arg, body params weren't visible —
        // that's what the post-OTA report flagged as transport failures.
        bool inBody = request->hasParam("duration", true);
        bool inQuery = request->hasParam("duration", false);
        if (!inBody && !inQuery) {
            request->send(400, "text/plain", "Missing duration param (query or form body)");
            return;
        }
        int dur = request->getParam("duration", inBody)->value().toInt();
        if (dur < 0 || dur > 255) {
            request->send(400, "text/plain", "Invalid duration (0-255)");
            return;
        }
        bool ok = _deps.radar->setParamConfig(
            _deps.radar->getMinGate(),
            _deps.radar->getMaxGate(),
            (uint8_t)dur
        );
        if (ok) {
            _deps.preferences->putUChar("radar_dur", (uint8_t)dur);
            request->send(200, "text/plain", "Timeout saved");
        } else {
            request->send(500, "text/plain", "Radar command failed");
        }
    });

    _deps.server->on("/api/radar/bluetooth", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        int state = _deps.radar->readRadarBluetoothState();
        doc["enabled"] = (state == 1);
        doc["configured"] = _deps.config->radar_bluetooth;
        doc["readable"] = (state >= 0);
        char macStr[18] = {0};
        if (_deps.radar->getRadarMacString(macStr)) {
            doc["mac"] = macStr;
        }
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/radar/bluetooth", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (!request->hasParam("enable", true)) {
            request->send(400, "text/plain", "Missing 'enable' param");
            return;
        }
        String v = request->getParam("enable", true)->value();
        bool enable = (v == "1" || v == "true");
        _deps.config->radar_bluetooth = enable;
        _deps.configManager->save();
        bool ok = _deps.radar->applyBluetoothState(enable);
        if (ok) {
            request->send(200, "text/plain",
                enable ? "Bluetooth enabled (radar restarted)"
                       : "Bluetooth disabled (radar restarted)");
        } else {
            request->send(500, "text/plain",
                "Config saved but radar apply failed — will retry on next boot");
        }
    });

    _deps.server->on("/api/radar/bluetooth", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        int state = _deps.radar->readRadarBluetoothState();
        doc["enabled"] = (state == 1);
        doc["configured"] = _deps.config->radar_bluetooth;
        doc["readable"] = (state >= 0);
        char macStr[18] = {0};
        if (_deps.radar->getRadarMacString(macStr)) {
            doc["mac"] = macStr;
        }
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/radar/bluetooth", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (!request->hasParam("enable", true)) {
            request->send(400, "text/plain", "Missing 'enable' param");
            return;
        }
        String v = request->getParam("enable", true)->value();
        bool enable = (v == "1" || v == "true");
        _deps.config->radar_bluetooth = enable;
        _deps.configManager->save();
        bool ok = _deps.radar->applyBluetoothState(enable);
        if (ok) {
            request->send(200, "text/plain",
                enable ? "Bluetooth enabled (radar restarted)"
                       : "Bluetooth disabled (radar restarted)");
        } else {
            request->send(500, "text/plain",
                "Config saved but radar apply failed — will retry on next boot");
        }
    });

#ifndef NO_BLUETOOTH
    _deps.server->on("/api/bluetooth/start", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (!_deps.bluetooth->isRunning()) {
            _deps.bluetooth->begin(_deps.config->mqtt_id, nullptr); // Passing nullptr for configManager as we use direct Preferences in implementation for now
            _deps.bluetooth->setTimeout(600); // 10 minutes
            request->send(200, "text/plain", "Bluetooth started for 10 minutes");
        } else {
            request->send(200, "text/plain", "Bluetooth already running");
        }
    });
#endif
}

void setupAlarmRoutes() {
    _deps.server->on("/api/alarm/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["armed"] = _deps.securityMonitor->isArmed();
        doc["state"] = _deps.securityMonitor->getAlarmStateStr();
        doc["current_zone"] = _deps.securityMonitor->getCurrentZoneName();
        doc["entry_delay"] = _deps.securityMonitor->getEntryDelay() / 1000;
        doc["exit_delay"] = _deps.securityMonitor->getExitDelay() / 1000;
        doc["debounce_frames"] = _deps.securityMonitor->getAlarmDebounceFrames();
        doc["disarm_reminder"] = _deps.securityMonitor->isDisarmReminderEnabled();
        if (_deps.securityMonitor->hasLastAlarmEvent()) {
            const AlarmTriggerEvent& evt = _deps.securityMonitor->getLastAlarmEvent();
            JsonObject last = doc["last_event"].to<JsonObject>();
            last["reason"]      = evt.reason;
            last["zone"]        = evt.zone;
            last["distance_cm"] = evt.distance_cm;
            last["energy_mov"]  = evt.energy_mov;
            last["energy_stat"] = evt.energy_stat;
            last["motion_type"] = evt.motion_type;
            last["uptime_s"]    = evt.uptime_s;
            if (evt.iso_time[0] != '\0') last["time"] = evt.iso_time;
        }
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/alarm/arm", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool immediate = request->hasParam("immediate") && request->getParam("immediate")->value() == "1";
        _deps.securityMonitor->setArmed(true, immediate);
        request->send(200, "text/plain", immediate ? "Armed (immediate)" : "Arming...");
    });

    _deps.server->on("/api/alarm/disarm", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        _deps.securityMonitor->setArmed(false);
        request->send(200, "text/plain", "Disarmed");
    });

    _deps.server->on("/api/alarm/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (request->hasParam("entry_delay")) {
            unsigned long val = request->getParam("entry_delay")->value().toInt() * 1000;
            _deps.securityMonitor->setEntryDelay(val);
            _deps.preferences->putULong("sec_entry_dl", val);
        }
        if (request->hasParam("exit_delay")) {
            unsigned long val = request->getParam("exit_delay")->value().toInt() * 1000;
            _deps.securityMonitor->setExitDelay(val);
            _deps.preferences->putULong("sec_exit_dl", val);
        }
        if (request->hasParam("disarm_reminder")) {
            bool en = request->getParam("disarm_reminder")->value() == "1";
            _deps.securityMonitor->setDisarmReminderEnabled(en);
            _deps.preferences->putBool("sec_dis_rem", en);
        }
        if (request->hasParam("debounce_frames")) {
            uint8_t val = (uint8_t)request->getParam("debounce_frames")->value().toInt();
            _deps.securityMonitor->setAlarmDebounceFrames(val);
            _deps.preferences->putUChar("sec_debounce", val);
        }
        request->send(200, "text/plain", "Alarm config saved");
    });
}

void setupLogRoutes() {
    _deps.server->on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        _deps.systemLog->getLogJSON(doc);
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/logs", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        _deps.systemLog->clear();
        request->send(200, "text/plain", "Logs cleared");
    });

    _deps.server->on("/api/events", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        uint32_t offset = request->hasParam("offset") ? request->getParam("offset")->value().toInt() : 0;
        uint32_t limit  = request->hasParam("limit")  ? request->getParam("limit")->value().toInt()  : 50;
        int8_t   type   = request->hasParam("type")   ? request->getParam("type")->value().toInt()   : -1;
        if (limit > 100) limit = 100; // cap
        JsonDocument doc;
        _deps.eventLog->getEventsJSON(doc, offset, limit, type);
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/events/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        _deps.eventLog->clear();
        request->send(200, "text/plain", "History cleared");
    });

    // CSV export of events
    _deps.server->on("/api/events/csv", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        _deps.eventLog->getEventsJSON(doc, 0, 500, -1);
        JsonArray arr = doc["events"].as<JsonArray>();

        String csv = "timestamp,type,distance_cm,energy,message\r\n";
        for (JsonObject obj : arr) {
            char line[128];
            snprintf(line, sizeof(line), "%u,%u,%u,%u,\"%s\"\r\n",
                obj["ts"].as<uint32_t>(),
                obj["type"].as<uint8_t>(),
                obj["dist"].as<uint16_t>(),
                obj["en"].as<uint8_t>(),
                obj["msg"].as<const char*>());
            csv += line;
        }
        AsyncWebServerResponse *response = request->beginResponse(200, "text/csv", csv);
        response->addHeader("Content-Disposition", "attachment; filename=\"events.csv\"");
        request->send(response);
    });

    // --- Network Config (Static IP / DHCP) ---
    _deps.server->on("/api/network/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["mode"] = strlen(_deps.config->static_ip) > 0 ? "static" : "dhcp";
        doc["ip"] = strlen(_deps.config->static_ip) > 0 ? _deps.config->static_ip : ETH.localIP().toString().c_str();
        doc["gateway"] = strlen(_deps.config->static_gw) > 0 ? _deps.config->static_gw : ETH.gatewayIP().toString().c_str();
        doc["subnet"] = _deps.config->static_mask;
        doc["dns"] = strlen(_deps.config->static_dns) > 0 ? _deps.config->static_dns : ETH.dnsIP().toString().c_str();
        doc["mac"] = ETH.macAddress();
        doc["link_speed"] = ETH.linkSpeed();
        doc["full_duplex"] = ETH.fullDuplex();
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/network/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool needsReboot = false;

        if (request->hasParam("mode")) {
            String mode = request->getParam("mode")->value();
            if (mode == "dhcp") {
                _deps.config->static_ip[0] = '\0';
                _deps.config->static_gw[0] = '\0';
                _deps.config->static_dns[0] = '\0';
                needsReboot = true;
            } else if (mode == "static") {
                if (request->hasParam("ip") && request->hasParam("gateway")) {
                    String ip = request->getParam("ip")->value();
                    String gw = request->getParam("gateway")->value();
                    strncpy(_deps.config->static_ip, ip.c_str(), sizeof(_deps.config->static_ip)-1);
                    strncpy(_deps.config->static_gw, gw.c_str(), sizeof(_deps.config->static_gw)-1);
                    if (request->hasParam("subnet")) {
                        strncpy(_deps.config->static_mask, request->getParam("subnet")->value().c_str(), sizeof(_deps.config->static_mask)-1);
                    }
                    if (request->hasParam("dns")) {
                        strncpy(_deps.config->static_dns, request->getParam("dns")->value().c_str(), sizeof(_deps.config->static_dns)-1);
                    }
                    needsReboot = true;
                }
            }
        }

        if (needsReboot) {
            _deps.configManager->save();
            request->send(200, "text/plain", "Network config saved, rebooting...");
            *_deps.shouldReboot = true;
        } else {
            request->send(400, "text/plain", "Invalid parameters");
        }
    });

    // --- Timezone Config ---
    _deps.server->on("/api/timezone", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["tz_offset"] = _deps.config->tz_offset;
        doc["dst_offset"] = _deps.config->dst_offset;
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/timezone", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool changed = false;
        if (request->hasParam("tz_offset")) {
            _deps.config->tz_offset = request->getParam("tz_offset")->value().toInt();
            changed = true;
        }
        if (request->hasParam("dst_offset")) {
            _deps.config->dst_offset = request->getParam("dst_offset")->value().toInt();
            changed = true;
        }
        if (changed) {
            _deps.configManager->save();
            request->send(200, "text/plain", "Timezone saved, rebooting...");
            *_deps.shouldReboot = true;
        } else {
            request->send(400, "text/plain", "No parameters");
        }
    });

    // --- Scheduled Arm/Disarm Config ---
    _deps.server->on("/api/schedule", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["arm_time"] = _deps.config->sched_arm_time;
        doc["disarm_time"] = _deps.config->sched_disarm_time;
        doc["auto_arm_minutes"] = _deps.config->auto_arm_minutes;
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    _deps.server->on("/api/schedule", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool changed = false;
        if (request->hasParam("arm_time")) {
            String t = request->getParam("arm_time")->value();
            strncpy(_deps.config->sched_arm_time, t.c_str(), sizeof(_deps.config->sched_arm_time)-1);
            changed = true;
        }
        if (request->hasParam("disarm_time")) {
            String t = request->getParam("disarm_time")->value();
            strncpy(_deps.config->sched_disarm_time, t.c_str(), sizeof(_deps.config->sched_disarm_time)-1);
            changed = true;
        }
        if (request->hasParam("auto_arm_minutes")) {
            int val = request->getParam("auto_arm_minutes")->value().toInt();
            if (val >= 0 && val <= 1440) {
                _deps.config->auto_arm_minutes = (uint16_t)val;
                changed = true;
            }
        }
        if (changed) {
            _deps.configManager->save();
            request->send(200, "text/plain", "Schedule saved");
        } else {
            request->send(400, "text/plain", "No parameters");
        }
    });
}

void setupWwwRoutes() {
    static File _uploadFile;

    // GET /api/www/info — current web asset metadata
    _deps.server->on("/api/www/info", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        bool hasFs = LittleFS.exists("/index.html.gz");
        doc["source"]   = hasFs ? "littlefs" : "progmem";
        doc["progmem_bytes"] = strlen(index_html);
        if (hasFs) {
            File f = LittleFS.open("/index.html.gz", "r");
            if (f) {
                doc["fs_bytes"] = f.size();
                doc["fs_mtime"] = (uint32_t)0; // LittleFS has no mtime
                f.close();
            }
        }
        doc["fs_free_kb"]  = (LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024;
        doc["fs_total_kb"] = LittleFS.totalBytes() / 1024;
        String resp; serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });

    // DELETE /api/www — remove LittleFS asset, revert to PROGMEM
    _deps.server->on("/api/www", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (LittleFS.exists("/index.html.gz")) {
            LittleFS.remove("/index.html.gz");
            request->send(200, "application/json", "{\"ok\":true,\"source\":\"progmem\"}");
        } else {
            request->send(404, "application/json", "{\"error\":\"no fs asset\"}");
        }
    });

    // POST /api/www/upload — upload index.html.gz to LittleFS
    _deps.server->on("/api/www/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!checkAuth(request)) return;
            bool ok = LittleFS.exists("/index.html.gz");
            if (ok) {
                request->send(200, "application/json", "{\"ok\":true}");
            } else {
                request->send(500, "application/json", "{\"error\":\"upload failed\"}");
            }
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index && !checkAuth(request)) return;
            if (!index) {
                DBG("WWW", "Upload start: %s (%u bytes)", filename.c_str(), request->contentLength());
                // Ensure /www directory exists (LittleFS creates on first write)
                if (_uploadFile) _uploadFile.close();
                _uploadFile = LittleFS.open("/index.html.gz", "w");
                if (!_uploadFile) {
                    DBG("WWW", "Cannot open /index.html.gz for write");
                    return;
                }
            }
            if (_uploadFile && len > 0) {
                _uploadFile.write(data, len);
            }
            if (final) {
                if (_uploadFile) {
                    DBG("WWW", "Upload done: %u bytes", index + len);
                    _uploadFile.close();
                } else {
                    DBG("WWW", "Upload final but file not open");
                }
            }
        }
    );
}

void setupSnapshotRoutes() {
    // GET /api/config/snapshots — list available snapshots
    _deps.server->on("/api/config/snapshots", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (!_deps.configSnapshot) {
            request->send(503, "application/json", "{\"error\":\"snapshots unavailable\"}");
            return;
        }
        JsonDocument doc;
        _deps.configSnapshot->getMetaJSON(doc);
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    // GET /api/config/snapshots/:slot — view content of one slot (passwords masked)
    _deps.server->on("/api/config/snapshots/0", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        if (!_deps.configSnapshot || !_deps.configSnapshot->getSnapshotJSON(doc, 0)) {
            request->send(404, "application/json", "{\"error\":\"slot 0 not found\"}");
            return;
        }
        String resp; serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });
    _deps.server->on("/api/config/snapshots/1", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        if (!_deps.configSnapshot || !_deps.configSnapshot->getSnapshotJSON(doc, 1)) {
            request->send(404, "application/json", "{\"error\":\"slot 1 not found\"}");
            return;
        }
        String resp; serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });
    _deps.server->on("/api/config/snapshots/2", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        if (!_deps.configSnapshot || !_deps.configSnapshot->getSnapshotJSON(doc, 2)) {
            request->send(404, "application/json", "{\"error\":\"slot 2 not found\"}");
            return;
        }
        String resp; serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });

    // POST /api/config/restore?slot=N — restore NVS from snapshot, then reboot
    _deps.server->on("/api/config/restore", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (!_deps.configSnapshot || !_deps.preferences) {
            request->send(503, "application/json", "{\"error\":\"snapshots unavailable\"}");
            return;
        }
        int slot = request->hasParam("slot") ? request->getParam("slot")->value().toInt() : -1;
        bool ok = _deps.configSnapshot->restoreSnapshot(_deps.preferences, slot);
        if (!ok) {
            request->send(400, "application/json", "{\"error\":\"restore failed\"}");
            return;
        }
        request->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
        *_deps.shouldReboot = true;
    });
}

// ============================================================================
// WiFi CSI Routes — /api/csi GET / POST / actions
// Conditionally compiled: USE_CSI defines real handlers, otherwise stubs
// return 503 so the GUI can show "not compiled in" instead of 404.
// ============================================================================
void setupCSIRoutes() {
#ifdef USE_CSI
    // GET — runtime config + live values + status
    _deps.server->on(AsyncURIMatcher::exact("/api/csi"), HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["compiled"] = true;
        doc["enabled"]  = _deps.config->csi_enabled;
        bool csiActive = (_deps.csiService != nullptr) && _deps.csiService->isActive();

        // Config (mirrors NVS-stored values)
        doc["threshold"]   = csiActive ? _deps.csiService->getThreshold() : _deps.config->csi_threshold;
        doc["hysteresis"]  = _deps.config->csi_hysteresis;
        doc["window"]      = csiActive ? _deps.csiService->getWindowSize() : _deps.config->csi_window;
        doc["publish_ms"]  = csiActive ? _deps.csiService->getPublishInterval() : _deps.config->csi_publish_ms;

        // Live values (only meaningful if active)
        if (csiActive) {
            doc["active"]      = true;
            doc["motion"]      = _deps.csiService->getMotionState();
            doc["composite"]   = _deps.csiService->getCompositeScore();
            doc["turbulence"]  = _deps.csiService->getTurbulence();
            doc["phase_turb"]  = _deps.csiService->getPhaseTurbulence();
            doc["ratio_turb"]  = _deps.csiService->getRatioTurbulence();
            doc["breathing"]   = _deps.csiService->getBreathingScore();
            doc["dser"]        = _deps.csiService->getDser();
            doc["plcr"]        = _deps.csiService->getPlcr();
            doc["variance"]    = _deps.csiService->getVariance();
            doc["packets"]     = (uint32_t)_deps.csiService->getPacketCount();
            doc["pps"]         = _deps.csiService->getPacketRate();
            doc["wifi_rssi"]   = _deps.csiService->getWifiRSSI();
            doc["wifi_ssid"]   = _deps.csiService->getWifiSSID();
            doc["idle_ready"]  = _deps.csiService->isIdleInitialized();
            doc["ht_ltf_seen"] = _deps.csiService->isHtLtfSeen();
            doc["traffic_gen"]  = _deps.csiService->isTrafficGenRunning();
            doc["traffic_port"] = _deps.csiService->getTrafficPort();
            doc["traffic_icmp"] = _deps.csiService->getTrafficICMP();
            doc["traffic_pps"]  = _deps.csiService->getTrafficRate();
            doc["wifi_ip"]      = WiFi.localIP().toString();
            doc["calibrating"] = _deps.csiService->isCalibrating();
            doc["calib_pct"]   = _deps.csiService->getCalibrationProgress();
            doc["learning_active"] = _deps.csiService->isSiteLearning();
            doc["learning_progress"] = _deps.csiService->getSiteLearningProgress() * 100.0f;
            doc["learning_elapsed_s"] = _deps.csiService->getSiteLearningElapsedSec();
            doc["learning_duration_s"] = _deps.csiService->getSiteLearningDurationSec();
            doc["learning_samples"] = _deps.csiService->getSiteLearningAcceptedSamples();
            doc["learning_rejected_motion"] = _deps.csiService->getSiteLearningRejectedMotion();
            doc["learning_threshold_estimate"] = _deps.csiService->getSiteLearningThresholdEstimate();
            doc["model_ready"] = _deps.csiService->hasLearnedSiteModel();
            doc["learned_threshold"] = _deps.csiService->getLearnedThreshold();
            doc["learned_mean_variance"] = _deps.csiService->getLearnedVarianceMean();
            doc["learned_std_variance"] = _deps.csiService->getLearnedVarianceStd();
            doc["learned_max_variance"] = _deps.csiService->getLearnedVarianceMax();
            doc["learned_samples"] = _deps.csiService->getLearnedSampleCount();
            doc["learn_refresh_count"] = _deps.csiService->getLearnRefreshCount();
        } else {
            doc["active"] = false;
        }

        // Fusion state
        doc["fusion_enabled"] = _deps.config->fusion_enabled;
        if (_deps.securityMonitor->isFusionActive()) {
            JsonObject fusion = doc["fusion"].to<JsonObject>();
            fusion["presence"]   = _deps.securityMonitor->isFusionPresence();
            fusion["confidence"] = _deps.securityMonitor->getFusionConfidence();
            fusion["source"]     = _deps.securityMonitor->getFusionSourceStr();
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    // POST /api/csi/calibrate — sample idle variance and auto-set threshold
    _deps.server->on("/api/csi/calibrate", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (_deps.csiService == nullptr || !_deps.csiService->isActive()) {
            request->send(503, "text/plain", "CSI service inactive");
            return;
        }
        uint32_t dur = 10000;
        if (request->hasParam("duration_ms")) {
            int d = request->getParam("duration_ms")->value().toInt();
            if (d >= 1000 && d <= 60000) dur = (uint32_t)d;
        }
        _deps.csiService->calibrateThreshold(dur);
        request->send(200, "text/plain", "Calibration started");
    });

    // POST /api/csi/site_learning — learn long-term quiet-site baseline and threshold
    _deps.server->on("/api/csi/site_learning", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (_deps.csiService == nullptr || !_deps.csiService->isActive()) {
            request->send(503, "text/plain", "CSI service inactive");
            return;
        }

        if (request->hasParam("stop")) {
            _deps.csiService->stopSiteLearning();
            request->send(200, "text/plain", "Site learning stopped");
            return;
        }

        if (request->hasParam("clear_model")) {
            _deps.csiService->clearLearnedSiteModel();
            request->send(200, "text/plain", "Learned site model cleared");
            return;
        }

        uint32_t durMs = 172800000UL; // 48h default
        if (request->hasParam("duration_h")) {
            float h = request->getParam("duration_h")->value().toFloat();
            // accept 5 min (0.083 h) to 168 h
            if (h >= 0.083f && h <= 168.0f) durMs = (uint32_t)(h * 3600000.0f);
        } else if (request->hasParam("duration_s")) {
            int s = request->getParam("duration_s")->value().toInt();
            if (s >= 60 && s <= 604800) durMs = (uint32_t)s * 1000UL;
        }

        _deps.csiService->startSiteLearning(durMs);
        request->send(200, "text/plain", "Site learning started");
    });

    // POST /api/csi/reset_baseline — clear idle baseline (use after moving sensor)
    _deps.server->on("/api/csi/reset_baseline", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (_deps.csiService == nullptr) {
            request->send(503, "text/plain", "CSI not available");
            return;
        }
        _deps.csiService->resetIdleBaseline();
        request->send(200, "text/plain", "Idle baseline reset");
    });

    // POST /api/csi/reconnect — force WiFi.reconnect() (useful if RSSI dropped)
    _deps.server->on("/api/csi/reconnect", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (_deps.csiService == nullptr) {
            request->send(503, "text/plain", "CSI not available");
            return;
        }
        _deps.csiService->forceReconnect();
        request->send(200, "text/plain", "Reconnect requested");
    });

    // GET /api/csi/wifi — returns configured SSID (never the password)
    _deps.server->on("/api/csi/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        const SystemConfig& cfg = _deps.configManager->getConfig();
        bool custom = (cfg.csi_ssid[0] != '\0');
        doc["ssid"]    = custom ? cfg.csi_ssid : CSI_WIFI_SSID;
        doc["custom"]  = custom;
        doc["has_pass"] = custom ? (cfg.csi_pass[0] != '\0') : (String(CSI_WIFI_PASS).length() > 0);
        String resp; serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });

    // POST /api/csi/wifi — save runtime SSID/pass to NVS (requires reboot to apply)
    // Body: ssid=...&pass=...   OR   reset=1 (clears stored, falls back to compile-time)
    _deps.server->on("/api/csi/wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        SystemConfig& cfg = _deps.configManager->getConfig();
        if (request->hasParam("reset", true) && request->getParam("reset", true)->value() == "1") {
            cfg.csi_ssid[0] = '\0';
            cfg.csi_pass[0] = '\0';
            _deps.configManager->save();
            request->send(200, "application/json", "{\"saved\":true,\"reboot_required\":true,\"reset\":true}");
            return;
        }
        if (!request->hasParam("ssid", true)) {
            request->send(400, "text/plain", "missing ssid");
            return;
        }
        String ssid = request->getParam("ssid", true)->value();
        if (ssid.length() == 0 || ssid.length() >= sizeof(cfg.csi_ssid)) {
            request->send(400, "text/plain", "ssid length 1-32");
            return;
        }
        String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : "";
        if (pass.length() >= sizeof(cfg.csi_pass)) {
            request->send(400, "text/plain", "pass length 0-64");
            return;
        }
        strncpy(cfg.csi_ssid, ssid.c_str(), sizeof(cfg.csi_ssid) - 1);
        cfg.csi_ssid[sizeof(cfg.csi_ssid) - 1] = '\0';
        strncpy(cfg.csi_pass, pass.c_str(), sizeof(cfg.csi_pass) - 1);
        cfg.csi_pass[sizeof(cfg.csi_pass) - 1] = '\0';
        _deps.configManager->save();
        request->send(200, "application/json", "{\"saved\":true,\"reboot_required\":true}");
    });

    // POST — update runtime config (any subset of params accepted)
    // Register as an exact match only: the AsyncWebServer default matcher is
    // backward-compatible and would otherwise also catch /api/csi/<action>.
    _deps.server->on(AsyncURIMatcher::exact("/api/csi"), HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool needsRestart = false;
        bool changed = false;

        if (request->hasParam("enabled")) {
            bool en = request->getParam("enabled")->value() == "1";
            if (en != _deps.config->csi_enabled) {
                _deps.config->csi_enabled = en;
                _deps.preferences->putBool("csi_en", en);
                needsRestart = true;  // begin()/teardown WiFi requires restart
                changed = true;
            }
        }

        if (request->hasParam("threshold")) {
            float thr = request->getParam("threshold")->value().toFloat();
            if (thr >= 0.001f && thr <= 100.0f) {
                _deps.config->csi_threshold = thr;
                _deps.preferences->putFloat("csi_thr", thr);
                if (_deps.csiService) _deps.csiService->setThreshold(thr);
                changed = true;
            }
        }

        if (request->hasParam("hysteresis")) {
            float hys = request->getParam("hysteresis")->value().toFloat();
            if (hys >= 0.1f && hys <= 0.99f) {
                _deps.config->csi_hysteresis = hys;
                _deps.preferences->putFloat("csi_hyst", hys);
                if (_deps.csiService) _deps.csiService->setHysteresis(hys);
                changed = true;
            }
        }

        if (request->hasParam("window")) {
            int w = request->getParam("window")->value().toInt();
            if (w >= 10 && w <= 200) {
                _deps.config->csi_window = (uint16_t)w;
                _deps.preferences->putUShort("csi_win", (uint16_t)w);
                if (_deps.csiService) _deps.csiService->setWindowSize((uint16_t)w);
                changed = true;
            }
        }

        if (request->hasParam("publish_ms")) {
            int p = request->getParam("publish_ms")->value().toInt();
            if (p >= 100 && p <= 60000) {
                _deps.config->csi_publish_ms = (uint16_t)p;
                _deps.preferences->putUShort("csi_pubms", (uint16_t)p);
                if (_deps.csiService) _deps.csiService->setPublishInterval((uint32_t)p);
                changed = true;
            }
        }

        // Traffic generator tuning — port, ICMP mode, PPS
        if (request->hasParam("traffic_port")) {
            int p = request->getParam("traffic_port")->value().toInt();
            if (p >= 1 && p <= 65535) {
                _deps.preferences->putUShort("csi_tport", (uint16_t)p);
                if (_deps.csiService) _deps.csiService->setTrafficPort((uint16_t)p);
                changed = true;
            }
        }
        if (request->hasParam("traffic_icmp")) {
            bool icmp = request->getParam("traffic_icmp")->value() == "1";
            _deps.preferences->putBool("csi_ticmp", icmp);
            if (_deps.csiService) _deps.csiService->setTrafficICMP(icmp);
            changed = true;
        }
        if (request->hasParam("traffic_pps")) {
            int pps = request->getParam("traffic_pps")->value().toInt();
            if (pps >= 10 && pps <= 500) {
                _deps.preferences->putUInt("csi_tpps", (uint32_t)pps);
                if (_deps.csiService) _deps.csiService->setTrafficRate((uint32_t)pps);
                changed = true;
            }
        }

        // ML MLP toggle + threshold — runtime + NVS persist
        if (request->hasParam("ml_enabled")) {
            bool en = request->getParam("ml_enabled")->value() == "1";
            _deps.preferences->putBool("csi_ml_en", en);
            if (_deps.csiService) _deps.csiService->setMlEnabled(en);
            changed = true;
        }
        if (request->hasParam("ml_threshold")) {
            float thr = request->getParam("ml_threshold")->value().toFloat();
            if (thr >= 0.05f && thr <= 0.95f) {
                _deps.preferences->putFloat("csi_ml_thr", thr);
                if (_deps.csiService) _deps.csiService->setMlThreshold(thr);
                changed = true;
            }
        }

        // Fusion enable/disable — takes effect immediately (no restart needed)
        if (request->hasParam("fusion_enabled")) {
            bool en = request->getParam("fusion_enabled")->value() == "1";
            if (en != _deps.config->fusion_enabled) {
                _deps.config->fusion_enabled = en;
                _deps.preferences->putBool("fus_en", en);
                if (!en) {
                    _deps.securityMonitor->setCSISource(nullptr);
                } else if (_deps.csiService && _deps.csiService->isActive()) {
                    _deps.securityMonitor->setCSISource(_deps.csiService);
                }
                changed = true;
            }
        }

        JsonDocument doc;
        doc["ok"] = changed;
        doc["needs_restart"] = needsRestart;
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

#else
    // No-CSI build — stubs so GUI can detect "not compiled in"
    _deps.server->on("/api/csi", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        request->send(200, "application/json",
                      "{\"compiled\":false,\"enabled\":false,\"active\":false}");
    });
    _deps.server->on("/api/csi", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        request->send(503, "text/plain", "CSI not compiled into this firmware");
    });
#endif
}

} // namespace WebRoutes
#endif // LITE_BUILD
