#include "services/LD2412Service.h"
#include "debug.h"
#include <esp_task_wdt.h>
#include <new>

LD2412Service::LD2412Service(int8_t rxPin, int8_t txPin)
    : _rxPin(rxPin), _txPin(txPin) {
    _mutex = xSemaphoreCreateMutex();
}

LD2412Service::~LD2412Service() {
    if (_radar) {
        delete _radar;
        _radar = nullptr;
    }
    if (_mutex) {
        vSemaphoreDelete(_mutex);
    }
}

bool LD2412Service::begin(HardwareSerial& serial, uint8_t minGate, uint8_t maxGate) {
    _serial = &serial;

    // 1. Try High Speed (256000) first - Performance Priority
    DBG("RADAR", "Attempting 256000 baud (High Speed)...");
    _serial->begin(256000, SERIAL_8N1, _rxPin, _txPin);
    delay(500);
    _radar = new(std::nothrow) LD2412(*_serial);
    if (!_radar) {
        DBG("RADAR", "CRIT: LD2412 alloc failed, heap=%u", ESP.getFreeHeap());
        return false;
    }

    bool connected = false;
    if (readFirmwareVersion()) {
        Serial.printf("[RADAR] Connected! Firmware: %d.%d (Baud: 256000)\n", _fwMajor, _fwMinor);
        _currentBaud = 256000;
        connected = true;
    } else {
        // 2. Try Standard Speed (115200)
        DBG("RADAR", "256000 failed, trying 115200...");
        delete _radar;
        _serial->end();
        delay(100);
        _serial->begin(115200, SERIAL_8N1, _rxPin, _txPin);
        delay(500);
        _radar = new(std::nothrow) LD2412(*_serial);
        if (!_radar) {
            DBG("RADAR", "CRIT: LD2412 alloc failed, heap=%u", ESP.getFreeHeap());
            return false;
        }

        if (readFirmwareVersion()) {
            Serial.printf("[RADAR] Connected! Firmware: %d.%d (Baud: 115200)\n", _fwMajor, _fwMinor);

            if (_fwMajor == 294) {
                // V1.26: UART setBaudRate ACKs but doesn't actually switch
                DBG("RADAR", "FW V1.26: Skipping baud upgrade (UART config broken).");
                connected = true;
            } else {
                // Normal FW: try upgrade to 256000
                DBG("RADAR", "Upgrading radar to 256000 baud for performance...");
                if (_radar->setBaudRate(256000)) {
                    delay(200); // Wait for radar to switch
                    _serial->end();
                    delay(100);
                    _serial->begin(256000, SERIAL_8N1, _rxPin, _txPin);
                    delay(500);
                    // Re-create object
                    delete _radar;
                    _radar = new LD2412(*_serial);

                    // Verify
                    if (readFirmwareVersion()) {
                        DBG("RADAR", "Upgrade successful! Running at 256000.");
                        _currentBaud = 256000;
                        connected = true;
                    } else {
                        DBG("RADAR", "Upgrade verification failed. Reverting to 115200...");
                        _serial->end();
                        _serial->begin(115200, SERIAL_8N1, _rxPin, _txPin);
                        delay(500);
                        delete _radar;
                        _radar = new LD2412(*_serial);
                        connected = true; // Fallback to 115200 is still "connected"
                    }
                } else {
                    DBG("RADAR", "Baud rate upgrade command failed. Staying at 115200.");
                    connected = true;
                }
            }
        }
    }

    if (connected) {
        _initSuccess = true;
        
        // TASK-012: Load configuration from radar
        loadConfigFromRadar();
        
        // CRITICAL: Force gate configuration BEFORE radar starts sending data
        DBG("RADAR", "Forcing gate config: Min=%d, Max=%d", minGate, maxGate);
        bool cfgRes = setParamConfig(minGate, maxGate, 10);
        if (cfgRes) {
            DBG("RADAR", "Gates configured successfully: %d - %d", minGate, maxGate);
        } else {
            Serial.println("[RADAR] WARNING: Failed to set gate config!"); // Keep as critical
        }

        // Auto-enable Engineering Mode
        if (_fwMajor == 294) {
            // V1.26: UART setEngineeringMode ACKs but radar never switches
            DBG("RADAR", "FW V1.26: Engineering Mode unavailable via UART. Running Basic Mode (4.2 Hz).");
            _engineeringMode = false;
        } else {
            // Normal FW: use UART command (3 attempts)
            DBG("RADAR", "Enabling Engineering Mode...");
            bool engOk = false;
            for (int attempt = 0; attempt < 3 && !engOk; attempt++) {
                esp_task_wdt_reset();
                engOk = _radar->setEngineeringMode(true);
                if (!engOk) {
                    DBG("RADAR", "Eng Mode attempt %d failed, retrying...", attempt + 1);
                    delay(200);
                }
            }
            if (engOk) {
                _engineeringMode = true;
                _engRecoveryAttempts = 0;
                DBG("RADAR", "Engineering Mode ENABLED (%.1f Hz expected)", 12.5f);
            } else {
                DBG("RADAR", "Engineering Mode failed - running in Basic Mode (4.2 Hz)");
            }
        }

        _lastValidData = millis();
        return true;
    }

    // Firmware read failed — passive mode, radar communication not confirmed
    Serial.println("[RADAR] Firmware query failed - passive mode (degraded)"); // Keep as critical
    _initSuccess = false;  // Handshake not confirmed
    _lastValidData = millis();
    return false;
}

void LD2412Service::stop() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (_radar) {
            delete _radar;
            _radar = nullptr;
        }
        if (_serial) {
            _serial->end();
        }
        DBG("RADAR", "Service stopped");
        xSemaphoreGive(_mutex);
    }
}


// =============================================================================
// Automatic Recovery (TASK-005)
// =============================================================================

void LD2412Service::attemptRadarRecovery() {
    unsigned long now = millis();

    // Rate limit recovery attempts (TASK-007 - overflow safe)
    if ((unsigned long)(now - _lastRecoveryAttempt) < RECOVERY_INTERVAL) {
        return;
    }
    _lastRecoveryAttempt = now;

    DBG("RADAR", "Recovery attempt %d/%d", _recoveryAttempts + 1, MAX_RECOVERY_ATTEMPTS);

    if (_recoveryAttempts < 2) {
        // Soft reset: Flush UART buffer and reset parser state
        DBG("RADAR", "Soft reset - flushing buffers");
        while (_serial->available()) {
            _serial->read();
        }
        // The new library will handle resync automatically
    }
    else if (_recoveryAttempts < 4) {
        // Medium reset: Restart radar module
        DBG("RADAR", "Medium reset - restarting radar module");
        esp_task_wdt_reset();
        if (_radar) {
            _radar->restartModule();
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // Non-blocking delay
        esp_task_wdt_reset();
    }
    else {
        // Hard reset: Re-initialize entire UART
        DBG("RADAR", "Hard reset - reinitializing UART");
        esp_task_wdt_reset();

        if (_radar) {
            delete _radar;
            _radar = nullptr;
        }

        _serial->end();
        vTaskDelay(pdMS_TO_TICKS(100)); // Non-blocking delay
        _serial->begin(_currentBaud, SERIAL_8N1, _rxPin, _txPin);
        vTaskDelay(pdMS_TO_TICKS(500)); // Non-blocking delay

        _radar = new LD2412(*_serial);
        esp_task_wdt_reset();

        // Restore engineering mode if it was active before hard reset
        if (_engineeringMode && _fwMajor != 294) {
            DBG("RADAR", "Hard reset: restoring Engineering Mode...");
            bool engOk = false;
            for (int i = 0; i < 3 && !engOk; i++) {
                esp_task_wdt_reset();
                engOk = _radar->setEngineeringMode(true);
                if (!engOk) vTaskDelay(pdMS_TO_TICKS(200));
            }
            if (!engOk) {
                DBG("RADAR", "Hard reset: Engineering Mode restore FAILED");
                _engineeringMode = false;
            }
        }

        // Reset recovery counter after hard reset
        _recoveryAttempts = 0;
        bool prevTamper = _tamperDetected;
        _tamperDetected = _tamperExternal || _tamperRadarFailure;
        if (_tamperDetected != prevTamper) _stateChanged = true;
    }

    _recoveryAttempts++;
}

// =============================================================================
// Main Update Loop
// =============================================================================

void LD2412Service::update() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;
    if (!_radar) { xSemaphoreGive(_mutex); return; }

    unsigned long now = millis();

    // Read current radar data — atomic snapshot (single readSerial call)
    RadarSnapshot snap = _radar->readSnapshot();
    int state = snap.state;

    // Check if we got valid data
    if (snap.valid && state >= 0) {
        _lastValidData = now;
        _recoveryAttempts = 0; // Reset recovery counter on success
        _tamperRadarFailure = false;
        bool prevTamper = _tamperDetected;
        _tamperDetected = _tamperExternal || _tamperRadarFailure;
        if (_tamperDetected != prevTamper) _stateChanged = true;

        // Get measurements from snapshot (all from same frame)
        int movDist = snap.movingDistance;
        int movEn = snap.movingEnergy;
        int statDist = snap.staticDistance;
        int statEn = snap.staticEnergy;

        // Update internal values
        _lastMovEn = (movEn >= 0) ? movEn : 0;
        _lastStatEn = (statEn >= 0) ? statEn : 0;

        // Update debug values
        _debugMovDist = (movDist >= 0) ? movDist : 0;
        _debugStatDist = (statDist >= 0) ? statDist : 0;
        _debugMovEn = _lastMovEn;
        _debugStatEn = _lastStatEn;

        // Determine detection state
        bool hasMoving = (state == 1 || state == 3);
        bool hasStatic = (state == 2 || state == 3);
        bool isDetected = (state > 0);

        // Distance priority: moving > static
        if (hasMoving && movDist > 0) {
            _lastDist = movDist;
        } else if (hasStatic && statDist > 0) {
            _lastDist = statDist;
        } else {
            _lastDist = 0;
        }

        // Reflector auto-learning — captures any persistent detection in empty room
        if (_learnActive) {
            if (now - _learnStart > (unsigned long)_learnDuration * 1000) {
                _learnActive = false;
                DBG("LEARN", "Done: %lu reflections / %lu total samples", _learnStaticSamples, _learnTotalSamples);
                _learnDone = true;  // flag pro main loop → Telegram notifikace
            } else {
                _learnTotalSamples++;
                // Any detection = potential reflector (room should be empty)
                bool hasMoving = (movEn > 5 && movDist > 0);
                bool hasStatic = (statEn > 5 && statDist > 0);

                if (hasMoving) {
                    uint8_t gate = (uint8_t)(movDist / _gateResolutionCm);
                    if (gate < 14) {
                        _learnGateCount[gate]++;
                        _learnStaticSamples++;
                        _learnEnergyType[gate] |= 0x02; // moving
                        if (movEn > _learnMaxEnergy[gate]) _learnMaxEnergy[gate] = movEn;
                    }
                }
                if (hasStatic) {
                    uint8_t gate = (uint8_t)(statDist / _gateResolutionCm);
                    if (gate < 14) {
                        _learnGateCount[gate]++;
                        if (!hasMoving) _learnStaticSamples++; // avoid double-counting
                        _learnEnergyType[gate] |= 0x01; // static
                        if (statEn > _learnMaxEnergy[gate]) _learnMaxEnergy[gate] = statEn;
                    }
                }
            }
        }

        // Debug print (TASK-013, TASK-024)
        if (_debugEnabled && (unsigned long)(now - _lastDebugPrint) > 1000) {
            _lastDebugPrint = now;
            DBG("DEBUG", "Mov: %dcm(E:%d), Stat: %dcm(E:%d) -> %dcm | UART:%s HP:%d%%",
                          movDist, movEn, statDist, statEn, _lastDist,
                          _radar->getUARTStateString(), _radar->getHealthScore());
        }

        // Motion type
        if (hasMoving && _lastMovEn > 10) {
            _currentMotionType = MotionType::MOVING;
        } else if (hasStatic) {
            _currentMotionType = MotionType::STATIONARY;
        } else {
            _currentMotionType = MotionType::NONE;
        }

        // Pet Immunity Filter (move + static)
        if (isDetected && _minMoveEnergy > 0 && _lastDist < 200) {
            bool movBelowThreshold = (!hasMoving || _lastMovEn < _minMoveEnergy);
            bool statBelowThreshold = (!hasStatic || _lastStatEn < _minMoveEnergy);
            if (movBelowThreshold && statBelowThreshold) {
                isDetected = false;
                DBG("SEC", "Pet Immunity: Ignored (Dist=%dcm MovE=%d StatE=%d < %d)",
                    _lastDist, _lastMovEn, _lastStatEn, _minMoveEnergy);
            }
        }

        // Note: Hardware anti-masking is handled by SecurityMonitor via processRadarData().
        // LD2412Service only handles presence state machine, not tamper logic.

        // Distance smoothing
        if (isDetected && _lastDist > 0) {
            _distHistory[_distIndex] = _lastDist;
            _distIndex = (_distIndex + 1) % DIST_SAMPLES;
            uint32_t sum = 0;
            uint8_t count = 0;
            for (int i = 0; i < DIST_SAMPLES; i++) {
                if (_distHistory[i] > 0) {
                    sum += _distHistory[i];
                    count++;
                }
            }
            if (count > 0) _lastDist = sum / count;
        } else {
            for (int i = 0; i < DIST_SAMPLES; i++) _distHistory[i] = 0;
        }

        // Motion direction calculation (TASK-007 - overflow safe)
        if ((unsigned long)(now - _lastTrendCalc) > 350) {
            if (isDetected && _lastDist > 0) {
                if (_trendStartDist == 0) {
                    _trendStartDist = _lastDist;
                    _currentDirection = MotionDirection::STATIC;
                } else {
                    int delta = (int)_lastDist - (int)_trendStartDist;
                    const int THRESHOLD = 10;
                    if (delta < -THRESHOLD) {
                        _currentDirection = MotionDirection::APPROACHING;
                    } else if (delta > THRESHOLD) {
                        _currentDirection = MotionDirection::RECEDING;
                    } else {
                        _currentDirection = MotionDirection::STATIC;
                    }
                    _trendStartDist = _lastDist;
                }
            } else {
                _currentDirection = MotionDirection::UNKNOWN;
                _trendStartDist = 0;
            }
            _lastTrendCalc = now;
        }

        // State machine with improved hold time logic
        switch (_currentState) {
            case PresenceState::IDLE:
                if (isDetected) {
                    _currentState = PresenceState::PRESENCE_DETECTED;
                    _lastDetectionTime = now;
                    _consecutiveMisses = 0;
                    _stateChanged = true;
                }
                break;

            case PresenceState::TAMPER:
                // Handled above
                break;

            case PresenceState::PRESENCE_DETECTED:
                if (isDetected) {
                    _lastDetectionTime = now;
                    _consecutiveMisses = 0;
                } else {
                    _consecutiveMisses++;
                    // Require 3 consecutive misses before entering HOLD
                    // This prevents single-frame dropouts from clearing detection
                    if (_consecutiveMisses >= 3) {
                        _holdStartTime = now;
                        _currentState = PresenceState::HOLD_TIMEOUT;
                        _stateChanged = true;
                    }
                }
                break;

            case PresenceState::HOLD_TIMEOUT:
                if (isDetected) {
                    _currentState = PresenceState::PRESENCE_DETECTED;
                    _lastDetectionTime = now;
                    _consecutiveMisses = 0;
                    _stateChanged = true;
                } else if ((unsigned long)(now - _holdStartTime) > _holdTime) {
                    _currentState = PresenceState::IDLE;
                    _stateChanged = true;
                }
                break;
        }
    } else {
        // No valid data - check for disconnection (TASK-006 - reduced to 2s)
        if ((unsigned long)(now - _lastValidData) > DISCONNECT_TIMEOUT) {
            // Attempt recovery
            attemptRadarRecovery();
        }
    }

    // Engineering Mode auto-recovery (with retry limit)
    if (_engineeringMode && _radar && _radar->isEngModeLost()) {
        if (_fwMajor == 294) {
            // V1.26: UART recovery impossible — eng mode was never truly enabled
            DBG("RADAR", "FW V1.26: Eng Mode lost flag spurious. Disabling.");
            _engineeringMode = false;
            _radar->clearEngModeLost();
        } else if (_engRecoveryAttempts < MAX_ENG_RECOVERY) {
            if ((unsigned long)(now - _lastEngRecovery) > 10000) {  // Max 1 attempt per 10s
                _lastEngRecovery = now;
                _engRecoveryAttempts++;
                DBG("RADAR", "Eng Mode lost! Recovery attempt %d/%d...", _engRecoveryAttempts, MAX_ENG_RECOVERY);
                bool ackOk = _setEngModeInternal(true);
                DBG("RADAR", "Eng Mode re-enable ACK: %s", ackOk ? "OK" : "FAIL");
                if (_engRecoveryAttempts >= MAX_ENG_RECOVERY) {
                    DBG("RADAR", "Eng Mode recovery exhausted (%d attempts). Falling back to Basic Mode.", MAX_ENG_RECOVERY);
                    _engineeringMode = false;
                    _radar->clearEngModeLost();
                }
            }
        }
    }

    xSemaphoreGive(_mutex);
}

// =============================================================================
// Data Access
// =============================================================================

RadarData LD2412Service::getData() const {
    RadarData d;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) { // TASK-014: increased from 50ms
        d.state = _tamperDetected ? PresenceState::TAMPER : _currentState;
        d.motion_type = _currentMotionType;
        d.motion_direction = _currentDirection;
        d.distance_cm = _lastDist;
        d.moving_energy = _lastMovEn;
        d.static_energy = _lastStatEn;
        d.tamper_alert = _tamperDetected;
        xSemaphoreGive(_mutex);
    } else {
        // FIX #9: Mark as invalid instead of injecting synthetic IDLE zeros
        d.state = PresenceState::IDLE;
        d.motion_type = MotionType::NONE;
        d.motion_direction = MotionDirection::UNKNOWN;
        d.distance_cm = 0;
        d.moving_energy = 0;
        d.static_energy = 0;
        d.tamper_alert = false;
        d.valid = false;
    }
    return d;
}

// =============================================================================
// Configuration Methods
// =============================================================================

void LD2412Service::loadConfigFromRadar() {
    if (!_radar) return;
    
    DBG("RADAR", "Loading configuration...");
    esp_task_wdt_reset();

    // 1. Get Parameter Config (Gates, Duration)
    int* params = _radar->getParamConfig();
    if (params) {
        _minGate = (uint8_t)params[0];
        _maxGate = (uint8_t)params[1];
        _maxGateDuration = (uint8_t)params[2];
        DBG("RADAR", "Config: MinGate=%d MaxGate=%d Duration=%ds",
                     _minGate, _maxGate, _maxGateDuration);
    }
    esp_task_wdt_reset();

    // 2. Get Motion Sensitivity
    int* movSens = _radar->getMotionSensitivity(RETURN_ARRAY);
    if (movSens) {
        for (int i = 0; i < 14; i++) {
            _motionSensitivity[i] = (uint8_t)movSens[i];
        }
        DBG("RADAR", "Motion sensitivity loaded");
    }
    esp_task_wdt_reset();

    // 3. Get Static Sensitivity
    int* statSens = _radar->getStaticSensitivity(RETURN_ARRAY);
    if (statSens) {
        for (int i = 0; i < 14; i++) {
            _staticSensitivity[i] = (uint8_t)statSens[i];
        }
        DBG("RADAR", "Static sensitivity loaded");
    }
    esp_task_wdt_reset();
}

bool LD2412Service::verifyGateConfig(uint8_t expectedMin, uint8_t expectedMax) {
    if (!_radar) return false;
    int* params = _radar->getParamConfig();
    if (!params) {
        DBG("RADAR", "Gate verify: getParamConfig failed");
        return false;
    }
    uint8_t actualMin = (uint8_t)params[0];
    uint8_t actualMax = (uint8_t)params[1];
    if (actualMin != expectedMin || actualMax != expectedMax) {
        Serial.printf("[RADAR] GATE CONFIG REVERTED! Expected %d-%d, got %d-%d. Re-applying...\n",
                      expectedMin, expectedMax, actualMin, actualMax);
        bool ok = setParamConfig(expectedMin, expectedMax, 10);
        if (ok) {
            DBG("RADAR", "Gate config re-applied successfully");
        } else {
            Serial.println("[RADAR] CRITICAL: Gate config re-apply FAILED!");
        }
        return false;
    }
    DBG("RADAR", "Gate config verified OK: %d-%d", actualMin, actualMax);
    return true;
}

bool LD2412Service::setMotionSensitivity(uint8_t sensitivity) {
    if (!_radar) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    esp_task_wdt_reset();
    bool result = _radar->setMotionSensitivity(sensitivity);
    esp_task_wdt_reset();
    
    if (result) {
        for (int i = 0; i < 14; i++) _motionSensitivity[i] = sensitivity;
    }
    xSemaphoreGive(_mutex);
    return result;
}

bool LD2412Service::setMotionSensitivity(uint8_t sens[14]) {
    if (!_radar) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    esp_task_wdt_reset();
    bool result = _radar->setMotionSensitivity(sens);
    esp_task_wdt_reset();

    if (result) {
        for (int i = 0; i < 14; i++) _motionSensitivity[i] = sens[i];
    }
    xSemaphoreGive(_mutex);
    return result;
}

bool LD2412Service::setStaticSensitivity(uint8_t sensitivity) {
    if (!_radar) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    esp_task_wdt_reset();
    bool result = _radar->setStaticSensitivity(sensitivity);
    esp_task_wdt_reset();

    if (result) {
        for (int i = 0; i < 14; i++) _staticSensitivity[i] = sensitivity;
    }
    xSemaphoreGive(_mutex);
    return result;
}

bool LD2412Service::setStaticSensitivity(uint8_t sens[14]) {
    if (!_radar) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    esp_task_wdt_reset();
    bool result = _radar->setStaticSensitivity(sens);
    esp_task_wdt_reset();

    if (result) {
        for (int i = 0; i < 14; i++) _staticSensitivity[i] = sens[i];
    }
    xSemaphoreGive(_mutex);
    return result;
}

bool LD2412Service::setParamConfig(uint8_t minGate, uint8_t maxGate, uint8_t duration) {
    if (!_radar) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    esp_task_wdt_reset();
    bool result = _radar->setParamConfig(minGate, maxGate, duration, 0);
    esp_task_wdt_reset();

    if (result) {
        _minGate = minGate;
        _maxGate = maxGate;
        _maxGateDuration = duration;
    }
    xSemaphoreGive(_mutex);
    return result;
}

bool LD2412Service::setResolution(float resolution) {
    if (!_radar) return false;

    // LD2412 resolution modes (ESPHome-compatible values):
    // mode 0 = 0.75m/gate (range 0-9m)
    // mode 1 = 0.50m/gate (range 0-6.5m)
    // mode 3 = 0.20m/gate (range 0-2.6m)  — NOT 2!
    uint8_t mode;
    if (resolution <= 0.25f) {
        mode = 3;  // 0.20m
    } else if (resolution <= 0.6f) {
        mode = 1;  // 0.50m
    } else {
        mode = 0;  // 0.75m (default)
    }

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    esp_task_wdt_reset();
    bool result = _radar->setResolution(mode);
    esp_task_wdt_reset();

    if (result) {
        DBG("RADAR", "Resolution mode set to %d (%.2fm)", mode, resolution);
        _gateResolutionCm = resolution * 100.0f;
    }
    xSemaphoreGive(_mutex);
    return result;
}

int LD2412Service::getResolution() {
    if (!_radar) return -1;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return -1;

    esp_task_wdt_reset();
    int result = _radar->getResolution();
    esp_task_wdt_reset();

    xSemaphoreGive(_mutex);
    return result;
}

bool LD2412Service::factoryReset() {
    if (!_radar) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    esp_task_wdt_reset();
    bool result = _radar->resetDeviceSettings();
    
    if (!result) {
        DBG("RADAR", "Standard factory reset failed, trying aggressive method...");
        // Aggressive method: send command directly without waiting for ACK from enableConfig
        _serial->flush();
        uint8_t enableCmd[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
        uint8_t resetCmd[]  = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xA2, 0x00, 0x04, 0x03, 0x02, 0x01};
        
        _serial->write(enableCmd, sizeof(enableCmd));
        delay(50);
        _serial->write(resetCmd, sizeof(resetCmd));
        delay(50);
        result = true; // Assume success of aggressive attempt
    }

    esp_task_wdt_reset();
    xSemaphoreGive(_mutex);
    return result;
}

bool LD2412Service::restartRadar() {
    if (!_radar) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    esp_task_wdt_reset();
    bool result = _radar->restartModule();
    
    if (!result) {
        DBG("RADAR", "Standard restart failed, trying aggressive method...");
        // Aggressive method: send restart command directly
        _serial->flush();
        uint8_t enableCmd[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
        uint8_t restartCmd[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xA3, 0x00, 0x04, 0x03, 0x02, 0x01};
        
        _serial->write(enableCmd, sizeof(enableCmd));
        delay(50);
        _serial->write(restartCmd, sizeof(restartCmd));
        delay(50);
        result = true;
    }

    esp_task_wdt_reset();
    xSemaphoreGive(_mutex);
    return result;
}

// =============================================================================
// Light Sensor Configuration (Task #11)
// =============================================================================

bool LD2412Service::setLightFunction(uint8_t mode) {
    if (!_radar) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    esp_task_wdt_reset();
    bool result = _radar->setLightFunction(mode);
    esp_task_wdt_reset();

    if (result) {
        DBG("RADAR", "Light function set to %d", mode);
    }
    xSemaphoreGive(_mutex);
    return result;
}

bool LD2412Service::setLightThreshold(uint8_t threshold) {
    if (!_radar) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    esp_task_wdt_reset();
    bool result = _radar->setLightThreshold(threshold);
    esp_task_wdt_reset();

    if (result) {
        DBG("RADAR", "Light threshold set to %d", threshold);
    }
    xSemaphoreGive(_mutex);
    return result;
}

int LD2412Service::getLightFunction() {
    if (!_radar) return -1;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return -1;

    esp_task_wdt_reset();
    int result = _radar->getLightFunction();
    esp_task_wdt_reset();

    xSemaphoreGive(_mutex);
    return result;
}

int LD2412Service::getLightThreshold() {
    if (!_radar) return -1;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return -1;

    esp_task_wdt_reset();
    int result = _radar->getLightThreshold();
    esp_task_wdt_reset();

    xSemaphoreGive(_mutex);
    return result;
}

void LD2412Service::setHoldTime(unsigned long ms) {
    _holdTime = ms;
}

unsigned long LD2412Service::getHoldTime() const {
    return _holdTime;
}

bool LD2412Service::startStaticLearn(uint16_t duration_s) {
    if (_learnActive) return false;
    memset(_learnGateCount, 0, sizeof(_learnGateCount));
    memset(_learnMaxEnergy, 0, sizeof(_learnMaxEnergy));
    memset(_learnEnergyType, 0, sizeof(_learnEnergyType));
    _learnTotalSamples  = 0;
    _learnStaticSamples = 0;
    _learnDuration      = duration_s;
    _learnStart         = millis();
    _learnActive        = true;
    DBG("LEARN", "Reflector learn started (%ds) — room must be empty!", duration_s);
    return true;
}

int LD2412Service::getLearnProgress() const {
    if (!_learnActive && _learnTotalSamples == 0) return -1; // not started
    if (!_learnActive) return 100;
    unsigned long elapsed = millis() - _learnStart;
    return (int)min(100UL, elapsed * 100 / ((unsigned long)_learnDuration * 1000));
}

void LD2412Service::getLearnResultJson(JsonDocument& doc) {
    doc["active"]   = _learnActive;
    doc["progress"] = getLearnProgress();
    doc["total_samples"]     = _learnTotalSamples;
    doc["reflection_samples"] = _learnStaticSamples;

    // Detection frequency (% of frames with any reflection)
    float reflFreq = (_learnTotalSamples > 0)
        ? (_learnStaticSamples * 100.0f / _learnTotalSamples) : 0.0f;
    doc["static_freq_pct"] = (int)reflFreq;

    // Per-gate breakdown
    JsonArray gates = doc["gates"].to<JsonArray>();
    for (int i = 0; i < 14; i++) {
        JsonObject g = gates.add<JsonObject>();
        g["gate"] = i;
        g["cm"]   = (int)(i * _gateResolutionCm);
        g["count"] = _learnGateCount[i];
        g["pct"]  = (_learnTotalSamples > 0)
            ? (int)(_learnGateCount[i] * 100 / _learnTotalSamples) : 0;
        g["max_energy"] = _learnMaxEnergy[i];
        // Type: "none", "static", "moving", "both"
        const char* typeStr = "none";
        if (_learnEnergyType[i] == 0x01) typeStr = "static";
        else if (_learnEnergyType[i] == 0x02) typeStr = "moving";
        else if (_learnEnergyType[i] == 0x03) typeStr = "both";
        g["type"] = typeStr;
    }

    // Top gate = most frequent reflector
    int topGate = 0;
    for (int i = 1; i < 14; i++) {
        if (_learnGateCount[i] > _learnGateCount[topGate]) topGate = i;
    }
    int confidence = (_learnTotalSamples > 0)
        ? (int)(_learnGateCount[topGate] * 100 / _learnTotalSamples) : 0;
    doc["top_gate"]   = topGate;
    doc["top_cm"]     = (int)(topGate * _gateResolutionCm);
    doc["confidence"] = confidence;
    doc["top_energy"] = _learnMaxEnergy[topGate];
    const char* topType = "none";
    if (_learnEnergyType[topGate] == 0x01) topType = "static";
    else if (_learnEnergyType[topGate] == 0x02) topType = "moving";
    else if (_learnEnergyType[topGate] == 0x03) topType = "both";
    doc["top_type"] = topType;

    // Suggested zone (±1 gate range)
    if (confidence >= 5 && _learnStaticSamples >= 10) {
        int minCm = max(0, (int)((topGate - 1) * _gateResolutionCm));
        int maxCm = (int)((topGate + 1) * _gateResolutionCm);
        doc["suggest_min_cm"] = minCm;
        doc["suggest_max_cm"] = maxCm;
        doc["suggest_ready"]  = true;
    } else {
        doc["suggest_ready"] = false;
    }
}

bool LD2412Service::startCalibration() {
    if (!_radar) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    esp_task_wdt_reset();
    bool result = _radar->enterCalibrationMode();
    esp_task_wdt_reset();

    xSemaphoreGive(_mutex);
    return result;
}

int LD2412Service::checkCalibrationStatus() {
    if (!_radar) return -1;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return -1;
    
    int ret = _radar->checkCalibrationMode();
    xSemaphoreGive(_mutex);
    return ret;
}

void LD2412Service::setMinMoveEnergy(uint8_t val) {
    _minMoveEnergy = val;
}

uint8_t LD2412Service::getMinMoveEnergy() const {
    return _minMoveEnergy;
}

void LD2412Service::setTamperDetected(bool tamper) {
    // Wait for mutex with longer timeout - tamper state is critical
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (_tamperExternal != tamper) {
            _tamperExternal = tamper;
            bool prevTamper = _tamperDetected;
            _tamperDetected = _tamperExternal || _tamperRadarFailure;
            if (_tamperDetected != prevTamper) {
                _stateChanged = true;
            }
        }
        xSemaphoreGive(_mutex);
    } else {
        // Mutex timeout - log warning, but DO NOT modify state without protection
        // This should not happen during normal operation
        DBG("RADAR", "WARNING: setTamperDetected mutex timeout!");
    }
}

// =============================================================================
// Health & Statistics (TASK-005, TASK-006, TASK-009)
// =============================================================================

bool LD2412Service::isRadarConnected() const {
    if (xSemaphoreTake(_mutex, 0) == pdTRUE) {
        bool result = _radar ? _radar->isConnected() : false;
        xSemaphoreGive(_mutex);
        return result;
    }
    return false;  // Mutex busy, assume disconnected (retry next cycle)
}

uint8_t LD2412Service::getHealthScore() const {
    if (xSemaphoreTake(_mutex, 0) == pdTRUE) {
        uint8_t result = _radar ? _radar->getHealthScore() : 0;
        xSemaphoreGive(_mutex);
        return result;
    }
    return 0;
}

const char* LD2412Service::getUARTStateString() const {
    if (xSemaphoreTake(_mutex, 0) == pdTRUE) {
        const char* src = _radar ? _radar->getUARTStateString() : "NO_RADAR";
        strncpy(_uartStateBuf, src, sizeof(_uartStateBuf) - 1);
        _uartStateBuf[sizeof(_uartStateBuf) - 1] = '\0';
        xSemaphoreGive(_mutex);
        return _uartStateBuf;
    }
    return "BUSY";
}

float LD2412Service::getFrameRate() const {
    if (xSemaphoreTake(_mutex, 0) == pdTRUE) {
        float result = _radar ? _radar->getStatistics().frameRate : 0.0f;
        xSemaphoreGive(_mutex);
        return result;
    }
    return 0.0f;
}

uint32_t LD2412Service::getErrorCount() const {
    if (xSemaphoreTake(_mutex, 0) == pdTRUE) {
        if (_radar) {
            const UARTStatistics& stats = _radar->getStatistics();
            uint32_t result = stats.invalidFrames + stats.checksumErrors + stats.timeouts;
            xSemaphoreGive(_mutex);
            return result;
        }
        xSemaphoreGive(_mutex);
    }
    return 0;
}

uint32_t LD2412Service::getValidFrameCount() const {
    if (xSemaphoreTake(_mutex, 0) == pdTRUE) {
        uint32_t result = _radar ? _radar->getStatistics().validFrames : 0;
        xSemaphoreGive(_mutex);
        return result;
    }
    return 0;
}

uint8_t LD2412Service::getLightLevelSafe() const {
    if (xSemaphoreTake(_mutex, 0) == pdTRUE) {
        int l = _radar ? _radar->getLightLevel() : 0;
        xSemaphoreGive(_mutex);
        return (l >= 0) ? (uint8_t)l : 0;
    }
    return 0;
}

// =============================================================================
// Firmware & Engineering Mode
// =============================================================================

bool LD2412Service::readFirmwareVersion() {
    if (!_radar) return false;
    esp_task_wdt_reset();
    int* fw = _radar->readFirmwareVersion();
    esp_task_wdt_reset();
    if (fw) {
        _fwMajor = fw[1];
        _fwMinor = fw[2];
        return true;
    }
    return false;
}

// Radar Bluetooth — HLK app pairing is a security risk for deployed units.
// Sentinel MAC 01:02:03:04:05:08 is returned by the radar when BT is disabled.
static bool _isNoMacSentinel(const uint8_t mac[6]) {
    static constexpr uint8_t NO_MAC[6] = {0x08, 0x05, 0x04, 0x03, 0x02, 0x01};
    for (uint8_t i = 0; i < 6; i++) if (mac[i] != NO_MAC[i]) return false;
    return true;
}

int LD2412Service::readRadarBluetoothState() {
    if (!_radar) return -1;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return -1;
    uint8_t mac[6] = {0};
    esp_task_wdt_reset();
    bool ok = _radar->readMacAddress(mac);
    esp_task_wdt_reset();
    xSemaphoreGive(_mutex);
    if (!ok) return -1;
    return _isNoMacSentinel(mac) ? 0 : 1;
}

bool LD2412Service::getRadarMacString(char out[18]) {
    if (!_radar || !out) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    uint8_t mac[6] = {0};
    esp_task_wdt_reset();
    bool ok = _radar->readMacAddress(mac);
    esp_task_wdt_reset();
    xSemaphoreGive(_mutex);
    if (!ok) {
        out[0] = '\0';
        return false;
    }
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

bool LD2412Service::applyBluetoothState(bool desired) {
    if (!_radar) return false;

    int cur = readRadarBluetoothState();
    if (cur < 0) {
        DBG("RADAR", "BT: cannot verify current state — skipping apply");
        return false;
    }
    if ((cur == 1) == desired) {
        DBG("RADAR", "BT already %s — no action", desired ? "ON" : "OFF");
        return true;
    }

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    Serial.printf("[RADAR] Bluetooth %s → %s (applying, radar will restart)\n",
                  cur ? "ON" : "OFF", desired ? "ON" : "OFF");
    esp_task_wdt_reset();
    bool setOk = _radar->setBluetooth(desired);
    bool restartOk = false;
    if (setOk) {
        restartOk = _radar->restartModule();
    }
    esp_task_wdt_reset();
    xSemaphoreGive(_mutex);

    if (!setOk) {
        DBG("RADAR", "BT: setBluetooth() failed");
        return false;
    }
    if (!restartOk) {
        DBG("RADAR", "BT: restartModule() failed — change may not be active");
    }
    // Give radar ~2 s to reboot, then verify via MAC read
    delay(2000);
    int verify = readRadarBluetoothState();
    if (verify < 0) {
        DBG("RADAR", "BT: post-apply verify failed (radar may still be rebooting)");
        return false;
    }
    return (verify == 1) == desired;
}

// Internal: call WITHOUT mutex (used from begin() and update() which already hold it)
bool LD2412Service::_setEngModeInternal(bool enable) {
    if (!_radar) return false;

    esp_task_wdt_reset();
    bool result = _radar->setEngineeringMode(enable);
    esp_task_wdt_reset();

    if (result) {
        _engineeringMode = enable;
        DBG("RADAR", "Engineering mode %s", enable ? "ENABLED" : "DISABLED");
    }
    return result;
}

bool LD2412Service::setEngineeringMode(bool enable) {
    if (!_radar) return false;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }

    bool result = _setEngModeInternal(enable);

    xSemaphoreGive(_mutex);
    return result;
}

bool LD2412Service::setTrackingMode(bool enable) {
    if (!_radar) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    
    esp_task_wdt_reset();
    bool result = _radar->enableTrackingMode(enable);
    esp_task_wdt_reset();
    
    if (result) {
        DBG("RADAR", "Tracking mode %s", enable ? "ENABLED" : "DISABLED");
    }
    xSemaphoreGive(_mutex);
    return result;
}

// =============================================================================
// Telemetry JSON
// =============================================================================

void LD2412Service::getGateEnergiesSafe(uint8_t movOut[14], uint8_t statOut[14]) const {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        memset(movOut, 0, 14);
        memset(statOut, 0, 14);
        return;
    }
    if (_radar) {
        const uint8_t* m = _radar->getAllMovingEnergies();
        const uint8_t* s = _radar->getAllStillEnergies();
        if (m) memcpy(movOut, m, 14); else memset(movOut, 0, 14);
        if (s) memcpy(statOut, s, 14); else memset(statOut, 0, 14);
    } else {
        memset(movOut, 0, 14);
        memset(statOut, 0, 14);
    }
    xSemaphoreGive(_mutex);
}

void LD2412Service::getTelemetryJson(JsonDocument& doc) {
    // Snapshot-under-lock pattern — minimise mutex hold time.
    // Under lock: copy POD values only. String/JSON building happens after release.
    PresenceState    snapState;
    MotionType       snapMotion;
    MotionDirection  snapDir;
    uint16_t snapDist;
    uint8_t  snapMovEn, snapStatEn;
    unsigned long snapHold;
    bool snapInitOk, snapTamper, snapEng, snapRadarEng, snapEngLost, snapConnected;
    uint8_t  snapEngRecovery;
    int snapFwMaj, snapFwMin;
    uint16_t snapMovDist, snapStatDist;
    float snapGateRes;
    uint8_t snapHealth = 0, snapLight = 0;
    uint8_t snapGateMov[14] = {0}, snapGateStill[14] = {0};
    UARTStatistics snapStats = {};
    const char* snapUartStateStr = "UNKNOWN";

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        doc["error"] = "mutex_timeout";
        return;
    }
    snapState       = _currentState;
    snapMotion      = _currentMotionType;
    snapDir         = _currentDirection;
    snapDist        = _lastDist;
    snapMovEn       = _lastMovEn;
    snapStatEn      = _lastStatEn;
    snapHold        = _holdTime;
    snapInitOk      = _initSuccess;
    snapTamper      = _tamperDetected;
    snapEng         = _engineeringMode;
    snapEngRecovery = _engRecoveryAttempts;
    snapFwMaj       = _fwMajor;
    snapFwMin       = _fwMinor;
    snapMovDist     = _debugMovDist;
    snapStatDist    = _debugStatDist;
    snapGateRes     = _gateResolutionCm;
    if (_radar) {
        snapConnected    = _radar->isConnected();
        snapHealth       = _radar->getHealthScore();
        snapUartStateStr = _radar->getUARTStateString();
        snapStats        = _radar->getStatistics();
        snapEngLost      = _radar->isEngModeLost();
        snapRadarEng     = _radar->isEngineeringMode();
        if (snapEng && snapRadarEng) {
            const uint8_t* m = _radar->getAllMovingEnergies();
            const uint8_t* s = _radar->getAllStillEnergies();
            if (m) memcpy(snapGateMov, m, 14);
            if (s) memcpy(snapGateStill, s, 14);
            snapLight = _radar->getLightLevel();
        }
    } else {
        snapConnected = false;
        snapEngLost   = false;
        snapRadarEng  = false;
    }
    xSemaphoreGive(_mutex);

    // -- JSON construction happens OUTSIDE the mutex --
    const char* stateStr =
        snapTamper ? "tamper" :
        (snapState == PresenceState::PRESENCE_DETECTED) ? "detected" :
        (snapState == PresenceState::HOLD_TIMEOUT)      ? "hold" :
        (snapState == PresenceState::TAMPER)            ? "tamper" : "idle";
    const char* mType =
        (snapMotion == MotionType::MOVING)     ? "moving" :
        (snapMotion == MotionType::STATIONARY) ? "stationary" : "none";
    const char* mDir =
        (snapDir == MotionDirection::APPROACHING) ? "approaching" :
        (snapDir == MotionDirection::RECEDING)    ? "receding" :
        (snapDir == MotionDirection::STATIC)      ? "static" : "unknown";

    doc["state"]         = stateStr;
    doc["motion_type"]   = mType;
    doc["motion_dir"]    = mDir;
    doc["distance_mm"]   = snapDist * 10;
    doc["moving_energy"] = snapMovEn;
    doc["static_energy"] = snapStatEn;
    doc["hold_time"]     = snapHold;
    doc["init_ok"]       = snapInitOk;
    doc["connected"]     = snapConnected;
    doc["fw_major"]      = snapFwMaj;
    doc["fw_minor"]      = snapFwMin;
    doc["raw_mov_dist"]  = snapMovDist;
    doc["raw_stat_dist"] = snapStatDist;
    doc["moving_gate"]   = (snapGateRes > 0) ? (uint8_t)(snapMovDist  / snapGateRes) : 0;
    doc["static_gate"]   = (snapGateRes > 0) ? (uint8_t)(snapStatDist / snapGateRes) : 0;
    if (snapTamper) doc["tamper"] = true;

    if (_radar) {
        doc["health_score"]    = snapHealth;
        doc["uart_state"]      = snapUartStateStr;
        doc["frame_rate"]      = snapStats.frameRate;
        doc["error_count"]     = snapStats.invalidFrames + snapStats.checksumErrors + snapStats.timeouts;
        doc["valid_frames"]    = snapStats.validFrames;
        doc["bytes_received"]  = snapStats.bytesReceived;
        doc["invalid_frames"]  = snapStats.invalidFrames;
        doc["checksum_errors"] = snapStats.checksumErrors;
        doc["timeouts"]        = snapStats.timeouts;
        doc["resync_count"]    = snapStats.resyncCount;
        doc["buffer_overflows"]= snapStats.bufferOverflows;
        doc["eng_mode_lost"]   = snapEngLost;
    }
    doc["engineering_mode"]      = snapEng;
    doc["eng_recovery_attempts"] = snapEngRecovery;

    if (snapEng && snapRadarEng) {
        JsonArray movJson   = doc["gate_move"].to<JsonArray>();
        JsonArray stillJson = doc["gate_still"].to<JsonArray>();
        for (int i = 0; i < 14; i++) {
            movJson.add(snapGateMov[i]);
            stillJson.add(snapGateStill[i]);
        }
        doc["light"] = snapLight;
    }
}
