#include "services/SecurityMonitor.h"
#include "services/MQTTService.h"
#include "services/TelegramService.h"
#ifdef USE_CSI
#include "services/CSIService.h"
#endif
#include "debug.h"
#include "constants.h"

static void fillISOTime(char* buf, size_t len) {
    struct tm t;
    if (getLocalTime(&t, 100) && t.tm_year > 100) { // year > 2000
        strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &t);
    } else {
        buf[0] = '\0';
    }
}

void SecurityMonitor::enqueueAlarmEvent() {
    if (_pendingEventCount < ALARM_QUEUE_SIZE) {
        uint8_t idx = (_pendingEventHead + _pendingEventCount) % ALARM_QUEUE_SIZE;
        _pendingEvents[idx] = _pendingEvent;
        _pendingEventCount++;
    } else {
        DBG("SecMon", "ALARM QUEUE FULL — event dropped! reason=%s", _pendingEvent.reason);
    }
    _lastAlarmEvent = _pendingEvent;
    _lastAlarmEventValid = true;
}

// rc2: Push a sample into the pre-trigger ring buffer (rate-limited to RING_PUSH_INTERVAL_MS)
void SecurityMonitor::pushRingSample(uint16_t distance, uint8_t mov, uint8_t stat,
                                     uint8_t fusionSrc, bool fusionPresence) {
    unsigned long now = millis();
    if (now - _lastRingPushMs < RING_PUSH_INTERVAL_MS) return;
    _lastRingPushMs = now;

    PreTriggerSample& s = _ringBuffer[_ringHead];
    s.age_ms = (uint16_t)(now & 0xFFFF); // store raw millis lo16; snapshot turns it into delta
    s.distance_cm = distance;
    s.move_energy = mov;
    s.static_energy = stat;
    s.fusion_source = fusionSrc;
    s.flags = (mov > 0 ? 0x1 : 0)
            | (stat > 0 ? 0x2 : 0)
            | (_isStaticFiltered ? 0x4 : 0)
            | (fusionPresence ? 0x8 : 0);

    _ringHead = (_ringHead + 1) % PRE_TRIGGER_WINDOW;
    if (_ringCount < PRE_TRIGGER_WINDOW) _ringCount++;
}

// rc2: Snapshot the ring buffer into an AlarmTriggerEvent, oldest→newest, with age relative to trigger
void SecurityMonitor::snapshotRingTo(AlarmTriggerEvent& evt, unsigned long triggerMs) const {
    evt.ring_count = _ringCount;
    if (_ringCount == 0) return;
    // Oldest sample is at (_ringHead - _ringCount + PRE_TRIGGER_WINDOW) % SIZE
    uint8_t startIdx = (uint8_t)((_ringHead + PRE_TRIGGER_WINDOW - _ringCount) % PRE_TRIGGER_WINDOW);
    uint16_t triggerLo = (uint16_t)(triggerMs & 0xFFFF);
    for (uint8_t i = 0; i < _ringCount; i++) {
        uint8_t src = (uint8_t)((startIdx + i) % PRE_TRIGGER_WINDOW);
        evt.ring[i] = _ringBuffer[src];
        // Convert stored lo16 millis to age_ms relative to trigger.
        // Ring push interval is 500 ms × 10 = 5 s window, well under the 65 s lo16 wrap, so subtract is safe.
        uint16_t sampleLo = _ringBuffer[src].age_ms;
        evt.ring[i].age_ms = (uint16_t)(triggerLo - sampleLo);
    }
}

static const char* motionTypeStr(uint8_t mov, uint8_t stat) {
    if (mov > 0 && stat > 0) return "both";
    if (mov > 0) return "moving";
    if (stat > 0) return "static";
    return "none";
}

// rc2: Fill the rc2 forensic fields on the staging _pendingEvent. Called at every trigger site.
static void fillForensicFieldsImpl(AlarmTriggerEvent& evt,
                                   const char* triggerSource,
                                   float fusionConf,
                                   bool staticFiltered,
                                   const char* zone,
                                   const char* prevZone) {
    strncpy(evt.trigger_source, triggerSource, sizeof(evt.trigger_source) - 1);
    evt.trigger_source[sizeof(evt.trigger_source) - 1] = '\0';
    evt.fusion_confidence = fusionConf;
    evt.static_filtered = staticFiltered ? 1 : 0;
    evt.zone_was_none = (!zone || zone[0] == '\0' || strcmp(zone, "none") == 0) ? 1 : 0;
    strncpy(evt.prev_zone, prevZone ? prevZone : "", sizeof(evt.prev_zone) - 1);
    evt.prev_zone[sizeof(evt.prev_zone) - 1] = '\0';
}

SecurityMonitor::SecurityMonitor() {
    _mutex = xSemaphoreCreateMutex();
}

void SecurityMonitor::begin(NotificationService* notifService, MQTTService* mqttService, TelegramService* telegramService, EventLog* eventLog, Preferences* prefs, const char* deviceLabel) {
    _notifService = notifService;
    _mqttService = mqttService;
    _telegramService = telegramService;
    _eventLog = eventLog;
    _prefs = prefs;
    if (deviceLabel) {
        strncpy(_deviceLabel, deviceLabel, sizeof(_deviceLabel) - 1);
        _deviceLabel[sizeof(_deviceLabel) - 1] = '\0';
    }
    _lastRSSI = 0;  // ETH — no RSSI
    _baselineRSSI = 0;
    _startTime = millis();
    DBG("SecMon", "Security Monitor initialized (label: %s)", _deviceLabel);
}

void SecurityMonitor::update() {
    // 500 ms — critical state transitions (PENDING→TRIGGERED, ARMING→ARMED)
    // must not be silently skipped due to mutex contention. The previous
    // 200 ms value left no headroom for processRadarData() bursts under CSI
    // fusion / heavy MQTT publish phases; setArmed() already uses 500 ms.
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        DBG("SecMon", "update() mutex timeout — state transitions deferred");
        return;
    }
    unsigned long now = millis();

    // Run health check every minute
    if (now - _lastHealthCheck > INTERVAL_HEALTH_CHECK_MS) {
        _lastHealthCheck = now;
        checkSystemHealth();
    }

    // Exit delay: ARMING -> ARMED (A4: presence warning at transition)
    if (_alarmState == AlarmState::ARMING && now - _exitDelayStart >= _exitDelay) {
        _alarmState = AlarmState::ARMED;
        DBG("SecMon", "ARMED (exit delay expired)");
        if (_lastDistance > 0) {
            triggerAlert(NotificationType::ALARM_STATE_CHANGE, "🔒 System ARMED", "⚠️ Presence still detected at activation! Distance: " + String(_lastDistance) + " cm");
        } else {
            triggerAlert(NotificationType::ALARM_STATE_CHANGE, "🔒 System ARMED", "Exit delay completed.");
        }
    }

    // A1: TRIGGERED timeout — auto-silence after configurable period
    if (_alarmState == AlarmState::TRIGGERED && _triggerTimeout > 0 && now - _triggerStartTime >= _triggerTimeout) {
        deactivateSiren();
        if (_autoRearm) {
            _alarmState = AlarmState::ARMED;
            clearApproachLog(); // FIX #7: Clear stale approach history from previous incident
            DBG("SecMon", "TRIGGERED timeout — auto-rearmed to ARMED");
            triggerAlert(NotificationType::ALARM_STATE_CHANGE, "🔕 Alarm auto-silenced", "Timeout " + String(_triggerTimeout / 60000) + " min. System re-ARMED.");
        } else {
            _alarmState = AlarmState::DISARMED;
            DBG("SecMon", "TRIGGERED timeout — disarmed");
            triggerAlert(NotificationType::ALARM_STATE_CHANGE, "🔕 Alarm auto-silenced", "Timeout " + String(_triggerTimeout / 60000) + " min. System DISARMED.");
        }
    }

    // Disarm reminder: presence detected while DISARMED
    if (_alarmState == AlarmState::DISARMED && _disarmReminderEnabled && _lastPresenceWhileDisarmed > 0) {
        if (now - _lastDisarmReminder > _disarmReminderInterval) {
            _lastDisarmReminder = now;
            triggerAlert(NotificationType::HEALTH_WARNING,
                "⚠️ System is still DISARMED",
                "Presence detected, but alarm is not active.\nUse /arm to activate.");
        }
    }
    if (_mutex) xSemaphoreGive(_mutex);
}

void SecurityMonitor::setArmed(bool armed, bool immediate, bool homeMode) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
    unsigned long now = millis();
    if (armed) {
        // FIX #1: Guard against re-arming while active — reject if PENDING or TRIGGERED
        if (_alarmState == AlarmState::PENDING || _alarmState == AlarmState::TRIGGERED) {
            DBG("SecMon", "setArmed(true) REJECTED — state is %s", getAlarmStateStr());
            if (_mutex) xSemaphoreGive(_mutex);
            return;
        }
        // Already arming/armed — idempotent, but allow upgrade/flip of homeMode flag
        if (_alarmState == AlarmState::ARMING || _alarmState == AlarmState::ARMED) {
            if (_homeMode != homeMode) {
                _homeMode = homeMode;
                DBG("SecMon", "setArmed(true) flip homeMode=%d (state=%s)", (int)homeMode, getAlarmStateStr());
                if (_prefs) _prefs->putBool("sec_home_mode", homeMode);
            } else {
                DBG("SecMon", "setArmed(true) ignored — already %s", getAlarmStateStr());
            }
            if (_mutex) xSemaphoreGive(_mutex);
            return;
        }
        _homeMode = homeMode;
        if (immediate) {
            _alarmState = AlarmState::ARMED;
            DBG("SecMon", "ARMED (immediate, home=%d)", (int)homeMode);
            triggerAlert(NotificationType::ALARM_STATE_CHANGE, "🔒 System ARMED", homeMode ? "Immediate activation (home)." : "Immediate activation.");
        } else {
            _alarmState = AlarmState::ARMING;
            _exitDelayStart = now;
            DBG("SecMon", "ARMING (exit delay %lu s, home=%d)", _exitDelay / 1000, (int)homeMode);
            triggerAlert(NotificationType::ALARM_STATE_CHANGE, "⏳ ARMING...", "Exit delay: " + String(_exitDelay / 1000) + "s" + (homeMode ? " (home)" : ""));
        }
        clearApproachLog();
        _armedDebounceCount = 0;
        _lastPresenceWhileDisarmed = 0;
        _presenceWhileDisarmedStart = 0;
        _lastDisarmReminder = 0;
        // Clear sticky reflector-zone filter so the exit delay starts fresh.
        // Without this, low-energy wobbles right before the schedule fired
        // could leave the filter latched and block legitimate motion until
        // STATIC_FILTER_CLEAR_FRAMES of sustained activity arrived.
        _isStaticFiltered = false;
        _staticFilterMoveFrames = 0;
    } else {
        AlarmState prev = _alarmState;
        // FIX #1: Always deactivate siren on disarm (covers TRIGGERED + any corrupted state)
        deactivateSiren();
        _alarmState = AlarmState::DISARMED;
        _homeMode = false;
        _entryDelayStart = 0;
        _exitDelayStart = 0;
        _lastPresenceWhileDisarmed = 0;
        _presenceWhileDisarmedStart = 0;
        _lastDisarmReminder = 0;
        DBG("SecMon", "DISARMED");
        if (prev != AlarmState::DISARMED) {
            triggerAlert(NotificationType::ALARM_STATE_CHANGE, "🔓 System DISARMED", "");
            strncpy(_pendingEvent.reason, "disarmed", sizeof(_pendingEvent.reason) - 1);
            strncpy(_pendingEvent.zone, _currentZoneName.c_str(), sizeof(_pendingEvent.zone) - 1);
            _pendingEvent.distance_cm = 0; _pendingEvent.energy_mov = 0; _pendingEvent.energy_stat = 0;
            strncpy(_pendingEvent.motion_type, "none", sizeof(_pendingEvent.motion_type) - 1);
            _pendingEvent.uptime_s = now / 1000; fillISOTime(_pendingEvent.iso_time, sizeof(_pendingEvent.iso_time));
            enqueueAlarmEvent();
        }
    }
    // Persist
    if (_prefs) {
        _prefs->putBool("sec_armed", armed);
        _prefs->putBool("sec_home_mode", armed ? _homeMode : false);
    }
    if (_mutex) xSemaphoreGive(_mutex);
}

const char* SecurityMonitor::getAlarmStateStr() const {
    switch (_alarmState) {
        case AlarmState::DISARMED:  return "disarmed";
        case AlarmState::ARMING:    return "arming";
        case AlarmState::ARMED:     return _homeMode ? "armed_home" : "armed_away";
        case AlarmState::PENDING:   return "pending";
        case AlarmState::TRIGGERED: return "triggered";
        default: return "disarmed";
    }
}

// checkRSSIAnomaly removed — POE board uses Ethernet, no RSSI.
// ETH link monitoring handled by connectivityTask in main.cpp.
void SecurityMonitor::checkRSSIAnomaly(long) {
    // no-op on POE
}

void SecurityMonitor::checkTamperState(bool isTamper) {
    unsigned long now = millis();

    // Tamper state changed from false to true
    if (isTamper && !_lastTamperState) {
        _tamperStartTime = now;

        if (now - _lastTamperAlert > COOLDOWN_TAMPER_ALERT_MS) {
            String msg = "🚨 TAMPER ALERT! Sensor may be obstructed or tampered with.";
            String details = "Immediate action required!\n";
            details += "Check sensor placement and surroundings.";

            DBG("SecMon", "TAMPER DETECTED!");
            triggerAlert(NotificationType::TAMPER_ALERT, msg, details);
            _lastTamperAlert = now;

            _lastEvent.tamper_detected = true;
            _lastEvent.last_event_time = now;
        }
    }

    // Tamper cleared
    if (!isTamper && _lastTamperState) {
        DBG("SecMon", "Tamper cleared");
        _lastEvent.tamper_detected = false;
    }

    _lastTamperState = isTamper;
}

void SecurityMonitor::checkRadarHealth(bool isConnected) {
    unsigned long now = millis();

    // Radar just disconnected
    if (!isConnected && _lastRadarConnected) {
        _radarDisconnectedTime = now;
        DBG("SecMon", "Radar disconnected");
    }

    // Radar been disconnected for more than 30 seconds
    if (!isConnected && (now - _radarDisconnectedTime > TIMEOUT_RADAR_DISCONNECT_MS)) {
        if (now - _lastRadarAlert > COOLDOWN_RADAR_ALERT_MS) {
            String msg = "Radar sensor connection lost";
            String details = "Duration: " + String((now - _radarDisconnectedTime) / 1000) + "s\n";
            details += "Check UART connection and power supply.";

            DBG("SecMon", "Radar offline for extended period");
            triggerAlert(NotificationType::SYSTEM_ERROR, msg, details);
            _lastRadarAlert = now;

            _lastEvent.radar_disconnected = true;
            _lastEvent.last_event_time = now;
        }
    }

    // Radar reconnected
    if (isConnected && !_lastRadarConnected) {
        DBG("SecMon", "Radar reconnected");
        _lastEvent.radar_disconnected = false;
    }

    _lastRadarConnected = isConnected;
}

const char* SecurityMonitor::getFusionSourceStr() const {
    // Bitmask: bit0=radar, bit1=CSI variance, bit2=CSI MLP
    switch (_fusionSource & 0x7) {
        case 0: return "none";
        case 1: return "radar";
        case 2: return "csi";
        case 3: return "radar+csi";
        case 4: return "ml";
        case 5: return "radar+ml";
        case 6: return "csi+ml";
        case 7: return "all";
        default: return "none";
    }
}

// -------------------------------------------------------------------------
// Security Pack v2.0 Implementation
// -------------------------------------------------------------------------
void SecurityMonitor::processRadarData(uint16_t distance, uint8_t move_energy, uint8_t static_energy) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    unsigned long now = millis();

    // csi10e: static filter is sticky — managed in the filter block below.
    // Do NOT unconditionally clear it here: brief move_energy spikes from
    // reflector wobble must not reset it within a single frame.

    // Pet Immunity — ignore low energy (move and static)
    if (_petImmunityThreshold > 0) {
        if (move_energy > 0 && move_energy < _petImmunityThreshold) {
            move_energy = 0;
        }
        if (static_energy > 0 && static_energy < _petImmunityThreshold) {
            static_energy = 0;
        }
    }

    // 1. Direction Detection (Approach vs Retreat)
    if (distance > 0 && _lastDistance > 0 && (now - _lastDistTime > 500)) { // Check every 0.5s
        int delta = (int)distance - (int)_lastDistance;

        if (abs(delta) > 20) { // Threshold 20cm to avoid jitter
            if (delta < 0) _lastDirection = "approaching";
            else _lastDirection = "retreating";
        } else {
            if (move_energy > 0 || static_energy > 0) _lastDirection = "stationary";
            else _lastDirection = "none";
        }

        _lastDistance = distance;
        _lastDistTime = now;
    } else if (distance > 0 && _lastDistance == 0) {
        _lastDistance = distance; // Init
        _lastDistTime = now;
    } else if (distance == 0) {
        _lastDirection = "none";
        _lastDistance = 0;
    }

    // 2. Anti-Masking (Blind Detection) - CONFIGURABLE
    // For empty locations (warehouses, cabins, server rooms) silence is normal
    if (move_energy == 0 && static_energy == 0) {
        if (_zeroEnergyStart == 0) _zeroEnergyStart = now;

        // Use configurable threshold (default 5 min)
        if (now - _zeroEnergyStart > _antiMaskThreshold) {
            if (!_isBlind) {
                _isBlind = true;
                DBG("SecMon", "Anti-Masking: Silence detected (>%lu min)", _antiMaskThreshold / MS_PER_MINUTE);

                // Only send notification if anti-masking is ENABLED
                if (_antiMaskEnabled) {
                    String msg = "⚠️ ANTI-MASKING: Sensor detects no activity!";
                    String details = "Possible tamper (sensor covered) or empty room.\n";
                    details += "Silence duration: " + String(_antiMaskThreshold / MS_PER_MINUTE) + " min";
                    triggerAlert(NotificationType::TAMPER_ALERT, msg, details);
                }
            }
        }
    } else {
        _zeroEnergyStart = 0;
        if (_isBlind) {
            _isBlind = false;
            DBG("SecMon", "Anti-Masking: Aktivita obnovena");
        }
    }

    // 3. Heartbeat / Periodic Report (configurable interval)
    if (_lastHeartbeat == 0) _lastHeartbeat = now;

    if (_heartbeatInterval > 0 && now - _lastHeartbeat > _heartbeatInterval) {
        _lastHeartbeat = now;

        unsigned long uptimeHours = millis() / MS_PER_HOUR;
        unsigned long uptimeDays = uptimeHours / 24;

        String msg = "🟢 Heartbeat: ONLINE & MONITORING";
        String details = "Uptime: ";
        if (uptimeDays > 0) {
            details += String(uptimeDays) + "d " + String(uptimeHours % 24) + "h";
        } else {
            details += String(uptimeHours) + "h";
        }
        details += "\nETH: " + String(ETH.linkUp() ? "UP" : "DOWN");  // ETH — no RSSI
        details += "\nStav: " + String(_isBlind ? "Silence (no activity)" : "Active");

        triggerAlert(NotificationType::HEALTH_WARNING, msg, details);
    }

    // 4. Loitering - CONFIGURABLE
    // Someone is close (< 2m) and staying for configurable time
    if (distance > 0 && distance < 200) {
        if (_loiterStart == 0) _loiterStart = now;

        if (now - _loiterStart > _loiterThreshold) {
            if (!_isLoitering) {
                _isLoitering = true;
                DBG("SecMon", "Loitering: Osoba <2m po dobu >%lu sec", _loiterThreshold / 1000);

                if (_loiterAlertEnabled) {
                    String msg = "👤 LOITERING: Someone lingering in close zone!";
                    String details = "Distance: " + String(distance) + " cm\n";
                    details += "Doba: >" + String(_loiterThreshold / 1000) + " sekund";
                    triggerAlert(NotificationType::PRESENCE_DETECTED, msg, details);
                } else {
                    DBG("SecMon", "Loitering detected but notifications are DISABLED in config");
                }
            }
        }
    } else {
        _loiterStart = 0;
        _isLoitering = false;
    }

    // 5. Zone Logic — MUST run BEFORE alarm trigger to use current zone behavior
    int newZoneIndex = -1;
    String newZoneName = "none";

    if (distance > 0) {
        for (size_t i = 0; i < _zones.size(); i++) {
            if (_zones[i].enabled && distance >= _zones[i].min_cm && distance <= _zones[i].max_cm) {
                newZoneIndex = i;
                newZoneName = String(_zones[i].name);
                break; // Use first matching zone (priority by order)
            }
        }
    }

    if (newZoneName != _currentZoneName) {
        _prevZoneName = _currentZoneName;  // Track previous zone for path validation
        _currentZoneName = newZoneName;
        _currentZoneIndex = newZoneIndex;
        _zoneEnterTime = now;
        _zoneAlertTriggered = false;
        DBG("SecMon", "Entered Zone: %s (prev: %s)", _currentZoneName.c_str(), _prevZoneName.c_str());

        // Publish via MQTT
        if (_mqttService && _mqttService->connected()) {
            _mqttService->publish(_mqttService->getTopics().current_zone, _currentZoneName.c_str(), true);
        }
    }

    if (_currentZoneIndex >= 0) {
        const AlertZone& z = _zones[_currentZoneIndex];
        if (now - _zoneEnterTime > z.delay_ms && !_zoneAlertTriggered) {
             _zoneAlertTriggered = true;
             if (z.alert_level > 0 && _alarmState != AlarmState::DISARMED && _alarmState != AlarmState::ARMING) {
                 // FIX #18: alert_level 3 used TAMPER_ALERT which shares cooldown with real tamper events
                 // Zone alerts are informational, use HEALTH_WARNING for high-level zones too
                 NotificationType nt = NotificationType::PRESENCE_DETECTED;
                 if (z.alert_level >= 2) nt = NotificationType::HEALTH_WARNING;
                 String msg = "Zone entry: " + String(z.name);
                 String details = "Distance: " + String(distance) + " cm";
                 triggerAlert(nt, msg, details);
             }
        }
    }

    // 4a2. Static reflector filter — independent of armed state
    // csi10e: sticky hysteresis. Once engaged in a behavior==3 zone, require
    // STATIC_FILTER_CLEAR_FRAMES consecutive frames of move_energy >= threshold
    // before clearing. This prevents reflector-wobble spikes (e.g. move=35 for
    // one frame) from bypassing the filter → no spurious pending/triggered.
    if (_currentZoneIndex >= 0 && (size_t)_currentZoneIndex < _zones.size() &&
        _zones[_currentZoneIndex].alarm_behavior == 3 && distance > 0) {
        if (move_energy < _alarmEnergyThreshold) {
            if (!_isStaticFiltered) {
                DBG("SecMon", "Static-filter ON zone '%s' (move=%d < thr=%d)",
                    _currentZoneName.c_str(), move_energy, _alarmEnergyThreshold);
            }
            _isStaticFiltered = true;
            _staticFilterMoveFrames = 0;
        } else {
            if (_staticFilterMoveFrames < 255) _staticFilterMoveFrames++;
            if (_isStaticFiltered && _staticFilterMoveFrames >= STATIC_FILTER_CLEAR_FRAMES) {
                _isStaticFiltered = false;
                DBG("SecMon", "Static-filter CLEAR zone '%s' (sustained move=%d, %u frames)",
                    _currentZoneName.c_str(), move_energy, _staticFilterMoveFrames);
            }
            // else: filter state sticky during debounce
        }
    } else {
        // Not in a reflector-filter zone → no sticky state
        _isStaticFiltered = false;
        _staticFilterMoveFrames = 0;
    }

    // ---- CSI Fusion: combined radar+WiFi presence decision ----
    // Must run BEFORE alarm logic so fusion can gate alarm transitions
#ifdef USE_CSI
    if (_csiService) {
        bool radarSees = (distance > 0 && (move_energy > 0 || static_energy > 0)) && !_isStaticFiltered;
        bool csiSees = _csiService->getMotionState();
        bool mlSees  = _csiService->isMlEnabled() && _csiService->getMlMotionState();
        float csiScore = _csiService->getCompositeScore();
        float csiBreathing = _csiService->getBreathingScore();
        float mlProb = _csiService->getMlProbability();

        // Source bitmask: bit0=radar, bit1=CSI variance, bit2=CSI MLP
        _fusionSource = (radarSees ? 1 : 0) | (csiSees ? 2 : 0) | (mlSees ? 4 : 0);

        if (radarSees && csiSees) {
            // BOTH primary signals agree — high confidence, MLP as booster
            _fusionPresence = true;
            _fusionConfidence = 0.9f + 0.1f * min(csiScore, 1.0f);
            if (mlSees) _fusionConfidence = 1.0f;  // triple agreement
            _csiSuppressCount = 0;
            _csiOnlyStart = 0;
        } else if (radarSees && !csiSees) {
            // Radar only, variance disagrees — MLP is tiebreaker
            if (mlSees) {
                // MLP sides with radar → likely real presence (static person, variance not yet tripped)
                _fusionPresence = true;
                _fusionConfidence = 0.6f + 0.2f * mlProb;
                _csiSuppressCount = 0;
            } else {
                // Neither CSI path agrees → possible false positive (fan, curtain)
                if (_csiSuppressCount < 255) _csiSuppressCount++;
                if (_csiSuppressCount >= CSI_SUPPRESS_FRAMES) {
                    _fusionPresence = false;
                    _fusionConfidence = 0.2f;
                    if (_csiSuppressCount == CSI_SUPPRESS_FRAMES || _csiSuppressCount % 200 == 0)
                        DBG("SecMon", "FUSION: radar suppressed (CSI+ML disagree %d frames)", _csiSuppressCount);
                } else {
                    _fusionPresence = true;
                    _fusionConfidence = 0.5f;
                }
            }
        } else if (!radarSees && csiSees) {
            // CSI variance without radar — MLP agreement cuts debounce (concordant ML vote)
            _csiSuppressCount = 0;

            if (csiScore < CSI_ONLY_MIN_SCORE && !mlSees) {
                _fusionPresence = false;
                _fusionConfidence = 0.1f;
                _csiOnlyStart = 0;
                DBG("SecMon", "FUSION: CSI-only rejected (score=%.2f < %.2f, ml=%d)", csiScore, CSI_ONLY_MIN_SCORE, mlSees);
            } else {
                if (_csiOnlyStart == 0) _csiOnlyStart = now;
                unsigned long csiOnlyDuration = now - _csiOnlyStart;
                // MLP concordance trims hold window (from 3000 ms down to 1000 ms)
                unsigned long holdMs = mlSees ? (CSI_ONLY_HOLD_MS / 3) : CSI_ONLY_HOLD_MS;

                if (csiOnlyDuration >= holdMs) {
                    _fusionPresence = true;
                    _fusionConfidence = 0.4f + 0.3f * min(csiScore, 1.0f);
                    if (csiBreathing > 0.1f) _fusionConfidence += 0.1f;
                    if (mlSees) _fusionConfidence += 0.2f * mlProb;
                    _fusionConfidence = min(_fusionConfidence, 1.0f);
                    DBG("SecMon", "FUSION: CSI hold (score=%.2f breath=%.3f ml=%.2f conf=%.2f dur=%lums/%lums)",
                        csiScore, csiBreathing, mlProb, _fusionConfidence, csiOnlyDuration, holdMs);
                } else {
                    _fusionPresence = false;
                    _fusionConfidence = 0.2f;
                    DBG("SecMon", "FUSION: CSI-only debounce (%lums / %lums ml=%d)", csiOnlyDuration, holdMs, mlSees);
                }
            }
        } else if (!radarSees && !csiSees && mlSees) {
            // MLP alone: the network sees motion that neither variance nor radar picked up.
            // Treat as weak signal — only elevate confidence when ml_probability is strongly above threshold.
            _csiSuppressCount = 0;
            _csiOnlyStart = 0;
            if (mlProb > (_csiService->getMlThreshold() + 0.2f)) {
                _fusionPresence = true;
                _fusionConfidence = 0.3f * mlProb;
                DBG("SecMon", "FUSION: ML-only (prob=%.2f conf=%.2f)", mlProb, _fusionConfidence);
            } else {
                _fusionPresence = false;
                _fusionConfidence = 0.1f;
            }
        } else {
            _fusionPresence = false;
            _fusionConfidence = 0.0f;
            _csiSuppressCount = 0;
            _csiOnlyStart = 0;
        }
    } else
#endif
    {
        // No CSI — fallback to radar-only presence
        bool radarSees = (distance > 0 && (move_energy > 0 || static_energy > 0)) && !_isStaticFiltered;
        _fusionPresence = radarSees;
        _fusionConfidence = radarSees ? 0.7f : 0.0f;
        _fusionSource = radarSees ? 1 : 0;
    }

    // 4b. Approach logging — record every detection while ARMED (forensic trail)
    if ((_alarmState == AlarmState::ARMED || _alarmState == AlarmState::PENDING) &&
        distance > 0 && (move_energy > 0 || static_energy > 0)) {
        logApproach(distance, move_energy, static_energy);
    }

    // rc2: Pre-trigger ring buffer — push every ~500 ms regardless of state, so the
    // window before any future trigger is always populated.
    pushRingSample(distance, move_energy, static_energy, _fusionSource, _fusionPresence);

    // 4c. Armed state: entry delay / triggered logic
    // Uses fusion result: radar FP (CSI disagrees) won't trigger alarm,
    // CSI-only presence (radar blind) CAN trigger alarm via entry delay.
    // FIX #4: Debounce — require N consecutive qualifying frames before transition
    // csi10e: radarQualifies respects the sticky static filter. Without this,
    // a brief move-energy spike could lift the filter in a behavior==3 zone
    // just long enough for this path to pass (and with ML=1.0 the entry delay
    // then expires immediately on the next PENDING check → TRIGGERED).
    bool radarQualifies = (distance > 0 && !_isStaticFiltered &&
        (move_energy >= _alarmEnergyThreshold || static_energy >= _alarmEnergyThreshold));
    bool csiOnlyQualifies = (!radarQualifies && _fusionPresence && _fusionSource == 2);
    bool armedQualifies = (_alarmState == AlarmState::ARMED &&
        _fusionPresence && (radarQualifies || csiOnlyQualifies));

    if (armedQualifies) {
        _armedDebounceCount++;
    } else if (_alarmState == AlarmState::ARMED) {
        _armedDebounceCount = 0;
    }

    if (armedQualifies && _armedDebounceCount >= _alarmDebounceFrames) {
        _armedDebounceCount = 0; // Reset for next detection

        uint8_t behavior = 0; // default = entry delay
        if (_currentZoneIndex >= 0 && (size_t)_currentZoneIndex < _zones.size()) {
            behavior = _zones[_currentZoneIndex].alarm_behavior;

            // Entry path validation: if zone requires a specific previous zone,
            // and the intruder came from a different path → force immediate trigger
            const char* reqPrev = _zones[_currentZoneIndex].valid_prev_zone;
            if (reqPrev[0] != '\0' && behavior == 0) {
                if (_prevZoneName != String(reqPrev)) {
                    DBG("SecMon", "INVALID PATH: zone '%s' requires prev '%s' but got '%s' → immediate",
                        _currentZoneName.c_str(), reqPrev, _prevZoneName.c_str());
                    behavior = 1; // Override to immediate trigger
                }
            }
        }

        // CSI-only detection: no zone info, always use entry delay
        if (csiOnlyQualifies) {
            behavior = 0;
            DBG("SecMon", "CSI-only alarm trigger (conf=%.2f, no radar distance)", _fusionConfidence);
        }

        const char* motionType = csiOnlyQualifies ? "csi" : motionTypeStr(move_energy, static_energy);
        const char* zoneName = csiOnlyQualifies ? "csi_only" : _currentZoneName.c_str();
        uint16_t evtDistance = csiOnlyQualifies ? 0 : distance;

        if (behavior == 2) {
            DBG("SecMon", "Detection in ignore-zone '%s', skipping alarm", _currentZoneName.c_str());
        } else if (behavior == 3) {
            // ignore_static_only: fixed reflectors (e.g. metal ladder) generate only static without movement
            if (move_energy < _alarmEnergyThreshold) {
                // _isStaticFiltered already set above
                DBG("SecMon", "Static-only in zone '%s' (move=%d < thr=%d), skip",
                    _currentZoneName.c_str(), move_energy, _alarmEnergyThreshold);
            } else {
                // Movement present → entry delay (real intruder)
                _alarmState = AlarmState::PENDING;
                _entryDelayStart = now;
                strncpy(_pendingEvent.reason, "entry_delay", sizeof(_pendingEvent.reason) - 1);
                strncpy(_pendingEvent.zone, zoneName, sizeof(_pendingEvent.zone) - 1);
                _pendingEvent.distance_cm = evtDistance; _pendingEvent.energy_mov = move_energy; _pendingEvent.energy_stat = static_energy;
                strncpy(_pendingEvent.motion_type, motionType, sizeof(_pendingEvent.motion_type) - 1);
                _pendingEvent.uptime_s = now / 1000; fillISOTime(_pendingEvent.iso_time, sizeof(_pendingEvent.iso_time));
                fillForensicFieldsImpl(_pendingEvent, getFusionSourceStr(), _fusionConfidence, _isStaticFiltered, zoneName, _prevZoneName.c_str());
                snapshotRingTo(_pendingEvent, now);
                enqueueAlarmEvent();
                DBG("SecMon", "PENDING — move in static-filter zone '%s'", _currentZoneName.c_str());
                triggerAlert(NotificationType::ENTRY_DETECTED, "⏳ ENTRY DETECTED!",
                    "Entry delay: " + String(_entryDelay / 1000) + "s\nZone: " + _currentZoneName + "\n" + formatApproachLog(), distance);
            }
        } else if (behavior == 1) {
            _alarmState = AlarmState::TRIGGERED;
            _triggerStartTime = now;
            strncpy(_pendingEvent.reason, "immediate", sizeof(_pendingEvent.reason) - 1);
            strncpy(_pendingEvent.zone, zoneName, sizeof(_pendingEvent.zone) - 1);
            _pendingEvent.distance_cm = evtDistance; _pendingEvent.energy_mov = move_energy; _pendingEvent.energy_stat = static_energy;
            strncpy(_pendingEvent.motion_type, motionType, sizeof(_pendingEvent.motion_type) - 1);
            _pendingEvent.uptime_s = now / 1000; fillISOTime(_pendingEvent.iso_time, sizeof(_pendingEvent.iso_time));
            fillForensicFieldsImpl(_pendingEvent, getFusionSourceStr(), _fusionConfidence, _isStaticFiltered, zoneName, _prevZoneName.c_str());
            snapshotRingTo(_pendingEvent, now);
            enqueueAlarmEvent();
            DBG("SecMon", "IMMEDIATE TRIGGER in zone '%s'!", zoneName);
            triggerAlert(NotificationType::ALARM_TRIGGERED, "🚨 ALARM TRIGGERED!", "Immediate trigger in zone: " + String(zoneName) + "\n" + formatApproachLog(), evtDistance);
            activateSiren();
        } else {
            _alarmState = AlarmState::PENDING;
            _entryDelayStart = now;
            strncpy(_pendingEvent.reason, "entry_delay", sizeof(_pendingEvent.reason) - 1);
            strncpy(_pendingEvent.zone, zoneName, sizeof(_pendingEvent.zone) - 1);
            _pendingEvent.distance_cm = evtDistance; _pendingEvent.energy_mov = move_energy; _pendingEvent.energy_stat = static_energy;
            strncpy(_pendingEvent.motion_type, motionType, sizeof(_pendingEvent.motion_type) - 1);
            _pendingEvent.uptime_s = now / 1000; fillISOTime(_pendingEvent.iso_time, sizeof(_pendingEvent.iso_time));
            fillForensicFieldsImpl(_pendingEvent, getFusionSourceStr(), _fusionConfidence, _isStaticFiltered, zoneName, _prevZoneName.c_str());
            snapshotRingTo(_pendingEvent, now);
            enqueueAlarmEvent();
            DBG("SecMon", "PENDING (entry delay %lu s, source=%s)", _entryDelay / 1000, getFusionSourceStr());
            triggerAlert(NotificationType::ENTRY_DETECTED, "⏳ ENTRY DETECTED!", "Entry delay: " + String(_entryDelay / 1000) + "s\nSource: " + String(getFusionSourceStr()) + "\n" + formatApproachLog() + "Use /disarm to deactivate.", evtDistance);
        }
    }
    else if (_alarmState == AlarmState::PENDING && now - _entryDelayStart >= _entryDelay) {
        _alarmState = AlarmState::TRIGGERED;
        _triggerStartTime = now;
        strncpy(_pendingEvent.reason, "entry_delay_expired", sizeof(_pendingEvent.reason) - 1);
        // zone/distance/energy from last PENDING event preserved — update uptime + refresh forensic snapshot
        _pendingEvent.uptime_s = now / 1000; fillISOTime(_pendingEvent.iso_time, sizeof(_pendingEvent.iso_time));
        fillForensicFieldsImpl(_pendingEvent, getFusionSourceStr(), _fusionConfidence, _isStaticFiltered,
                               _pendingEvent.zone, _prevZoneName.c_str());
        snapshotRingTo(_pendingEvent, now);
        enqueueAlarmEvent();
        DBG("SecMon", "ALARM TRIGGERED!");
        triggerAlert(NotificationType::ALARM_TRIGGERED, "🚨 ALARM TRIGGERED!", "Entry delay expired!\n" + formatApproachLog(), _pendingEvent.distance_cm);
        activateSiren();
    }

    // Track sustained presence while disarmed (for reminder) — use fusion result
    if (_alarmState == AlarmState::DISARMED && _fusionPresence) {
        if (_presenceWhileDisarmedStart == 0) {
            _presenceWhileDisarmedStart = now;
        } else if (now - _presenceWhileDisarmedStart > 10000) {
            _lastPresenceWhileDisarmed = now;
        }
    } else {
        _presenceWhileDisarmedStart = 0;
    }
    if (_mutex) xSemaphoreGive(_mutex);
}

void SecurityMonitor::checkSystemHealth() {
    bool healthy = true;

    // Check Ethernet (ETH — no WiFi)
    if (!ETH.linkUp()) {
        DBG("SecMon", "Health Check: ETH link down");
        healthy = false;
    }

    // Check free heap
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < HEAP_LOW_WARNING) {
        DBG("SecMon", "Health Check: Low memory (%u bytes)", freeHeap);
        healthy = false;

        if (!_systemHealthy) {  // Was already unhealthy, send alert
            String msg = "System health warning: Low memory";
            String details = "Free heap: " + String(freeHeap) + " bytes";
            triggerAlert(NotificationType::HEALTH_WARNING, msg, details);
        }
    }

    // Check certificate expiry (once per day)
    static unsigned long lastCertCheck = 0;
    unsigned long now = millis();
    if (_mqttService && _mqttService->connected() && (now - lastCertCheck > INTERVAL_CERT_CHECK_MS)) {
        DBG("SecMon", "Checking MQTT certificate expiry...");
        _mqttService->checkCertificateExpiry();
        lastCertCheck = now;
    }

    if (healthy && !_systemHealthy) {
        DBG("SecMon", "System health restored");
    }

    _systemHealthy = healthy;
}

void SecurityMonitor::triggerAlert(NotificationType type, const String& message, const String& details, int16_t explicitDist) {
    // Log to EventLog if available
    if (_eventLog) {
        uint8_t evtType = EVT_SYSTEM;
        switch (type) {
            case NotificationType::PRESENCE_DETECTED: evtType = EVT_PRESENCE; break;
            case NotificationType::TAMPER_ALERT: evtType = EVT_TAMPER; break;
            case NotificationType::WIFI_ANOMALY: evtType = EVT_WIFI; break;
            case NotificationType::HEALTH_WARNING: evtType = EVT_HEARTBEAT; break;
            case NotificationType::ALARM_STATE_CHANGE: evtType = EVT_SECURITY; break;
            case NotificationType::ENTRY_DETECTED: evtType = EVT_SECURITY; break;
            case NotificationType::ALARM_TRIGGERED: evtType = EVT_SECURITY; break;
            default: evtType = EVT_SYSTEM; break;
        }
        // FIX #3: Use explicit distance when provided, not stale _lastDistance
        uint16_t logDist = (explicitDist >= 0) ? (uint16_t)explicitDist : _lastDistance;
        _eventLog->addEvent(evtType, logDist, 0, message.c_str());
        // FIX #16: Immediately persist critical security events
        if (evtType == EVT_SECURITY) {
            _eventLog->flushNow();
        }
    }

    // Try NotificationService first (has cooldown, filtering, multi-channel)
    if (_notifService && _notifService->isEnabled()) {
        _notifService->sendAlert(type, message, details);
        return;
    }

    // Fallback: send directly via TelegramService if NotificationService is disabled
    // BUT MUST respect individual feature toggles!
    if (_telegramService && _telegramService->isEnabled()) {
        bool shouldSend = true;

        if (type == NotificationType::PRESENCE_DETECTED && !_loiterAlertEnabled) shouldSend = false;
        if (type == NotificationType::HEALTH_WARNING && message.indexOf("Blind") >= 0 && !_antiMaskEnabled) shouldSend = false;
        // ALARM_STATE_CHANGE and ENTRY_DETECTED always send (security-critical)

        if (shouldSend) {
            String fullMsg;
            if (strlen(_deviceLabel) > 0) {
                fullMsg = "[" + String(_deviceLabel) + "] " + message;
            } else {
                fullMsg = message;
            }
            if (details.length() > 0) {
                fullMsg += "\n" + details;
            }
            _telegramService->sendMessage(fullMsg);
        }
    }
}

// --- Siren GPIO ---

void SecurityMonitor::setSirenPin(int8_t pin) {
    _sirenPin = pin;
    if (_sirenPin >= 0) {
        pinMode(_sirenPin, OUTPUT);
        digitalWrite(_sirenPin, LOW);
        DBG("SecMon", "Siren pin configured: GPIO %d", _sirenPin);
    }
}

void SecurityMonitor::activateSiren() {
    if (_sirenPin >= 0 && !_sirenActive) {
        digitalWrite(_sirenPin, HIGH);
        _sirenActive = true;
        DBG("SecMon", "SIREN ON (GPIO %d)", _sirenPin);
    }
}

void SecurityMonitor::deactivateSiren() {
    if (_sirenPin >= 0 && _sirenActive) {
        digitalWrite(_sirenPin, LOW);
        _sirenActive = false;
        DBG("SecMon", "SIREN OFF (GPIO %d)", _sirenPin);
    }
}

// --- Approach Tracker ---

void SecurityMonitor::logApproach(uint16_t dist, uint8_t move_en, uint8_t stat_en) {
    time_t epoch = time(nullptr);
    uint32_t ts = (epoch > 1700000000) ? (uint32_t)epoch : millis() / 1000;

    // Deduplicate: skip if same distance as last entry and less than 2s apart
    if (_approachCount > 0) {
        uint8_t lastIdx = (_approachHead + _approachCount - 1) % APPROACH_LOG_SIZE;
        if (_approachLog[lastIdx].distance_cm == dist && ts - _approachLog[lastIdx].timestamp_s < 2) {
            return;
        }
    }

    uint8_t idx = (_approachHead + _approachCount) % APPROACH_LOG_SIZE;
    if (_approachCount >= APPROACH_LOG_SIZE) {
        // Full — overwrite oldest
        _approachHead = (_approachHead + 1) % APPROACH_LOG_SIZE;
    } else {
        _approachCount++;
    }
    _approachLog[idx] = { ts, dist, move_en, stat_en };
}

void SecurityMonitor::clearApproachLog() {
    _approachHead = 0;
    _approachCount = 0;
}

String SecurityMonitor::formatApproachLog() const {
    if (_approachCount == 0) return "No data";

    String result = "📍 Approach (" + String(_approachCount) + " entries):\n";
    uint32_t firstTs = _approachLog[_approachHead].timestamp_s;
    bool isEpoch = firstTs > 1700000000;

    for (uint8_t i = 0; i < _approachCount; i++) {
        uint8_t idx = (_approachHead + i) % APPROACH_LOG_SIZE;
        const ApproachEntry& e = _approachLog[idx];

        if (isEpoch) {
            // Show HH:MM:SS from epoch
            time_t t = (time_t)e.timestamp_s;
            struct tm tm;
            localtime_r(&t, &tm);
            char timeBuf[9];
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm);
            result += String(timeBuf);
        } else {
            // Show relative seconds from first entry
            result += "T+" + String(e.timestamp_s - firstTs) + "s";
        }
        result += " | " + String(e.distance_cm) + "cm";
        result += " M:" + String(e.move_energy) + " S:" + String(e.static_energy);
        result += "\n";
    }
    return result;
}
