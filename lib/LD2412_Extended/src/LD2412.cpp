/**
 * @file LD2412.cpp
 * @author Trent Tobias (original)
 * @see Upstream: https://github.com/tobiastl/LD2412 (MIT, Copyright (c) 2025 tobiastl)
 * @version 2.0.0
 * @date January 27, 2026
 * @brief LD2412 serial communication - Enhanced for 24/7 security systems
 */

#include "LD2412.h"

LD2412::LD2412(Stream& ld_serial) : serial(ld_serial) {
    _stats.reset();
    ringClear();
    _uartState = UARTState::DISCONNECTED;
    _stateEntryTime = millis();
}

// =============================================================================
// Ring Buffer Implementation (TASK-001)
// =============================================================================

void LD2412::ringPush(uint8_t byte) {
    portENTER_CRITICAL(&_ringMux);
    uint16_t next = (_ringHead + 1) % RING_BUFFER_SIZE;
    if (next != _ringTail) {
        _ringBuffer[_ringHead] = byte;
        _ringHead = next;
    } else {
        _stats.bufferOverflows++;
    }
    portEXIT_CRITICAL(&_ringMux);
}

// Batch push: single critical section for multiple bytes (reduces lock overhead)
void LD2412::ringPushBatch(const uint8_t* data, size_t len) {
    portENTER_CRITICAL(&_ringMux);
    for (size_t i = 0; i < len; i++) {
        uint16_t next = (_ringHead + 1) % RING_BUFFER_SIZE;
        if (next != _ringTail) {
            _ringBuffer[_ringHead] = data[i];
            _ringHead = next;
        } else {
            _stats.bufferOverflows += (len - i); // count all remaining as lost
            break;
        }
    }
    portEXIT_CRITICAL(&_ringMux);
}

bool LD2412::ringPop(uint8_t& byte) {
    portENTER_CRITICAL(&_ringMux);
    if (_ringHead == _ringTail) {
        portEXIT_CRITICAL(&_ringMux);
        return false;
    }
    byte = _ringBuffer[_ringTail];
    _ringTail = (_ringTail + 1) % RING_BUFFER_SIZE;
    portEXIT_CRITICAL(&_ringMux);
    return true;
}

uint16_t LD2412::ringAvailable() const {
    portENTER_CRITICAL(&_ringMux);
    uint16_t avail = (_ringHead - _ringTail + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;
    portEXIT_CRITICAL(&_ringMux);
    return avail;
}

void LD2412::ringClear() {
    portENTER_CRITICAL(&_ringMux);
    _ringHead = 0;
    _ringTail = 0;
    portEXIT_CRITICAL(&_ringMux);
}

// =============================================================================
// UART State Machine (TASK-004)
// =============================================================================

void LD2412::transitionState(UARTState newState) {
    if (_uartState != newState) {
        _uartState = newState;
        _stateEntryTime = millis();
        _consecutiveErrors = 0;
    }
}

void LD2412::updateUARTState() {
    unsigned long now = millis();

    switch (_uartState) {
        case UARTState::DISCONNECTED:
            // Transition to WAITING_SYNC if we have data
            if (serial.available() > 0) {
                transitionState(UARTState::WAITING_SYNC);
            }
            break;

        case UARTState::WAITING_SYNC:
            // If no valid frame for 5 seconds, stay disconnected
            if (timeElapsed(_stateEntryTime, 5000)) {
                transitionState(UARTState::DISCONNECTED);
            }
            // Will transition to SYNCED when header is found (in readSerialImproved)
            break;

        case UARTState::SYNCED:
            // Timeout if we don't complete frame within 500ms
            if (timeElapsed(_stateEntryTime, 500)) {
                _stats.timeouts++;
                _consecutiveErrors++;
                transitionState(UARTState::WAITING_SYNC);
            }
            break;

        case UARTState::RUNNING:
            // Check for degradation (high error rate)
            if (_stats.getErrorRate() > 0.1f && _stats.validFrames > 100) {
                transitionState(UARTState::DEGRADED);
            }
            // Check for disconnection (no frames for 3 seconds)
            if (timeElapsed(_lastValidFrameTime, 3000)) {
                transitionState(UARTState::DISCONNECTED);
            }
            break;

        case UARTState::DEGRADED:
            // Recovery if error rate drops
            if (_stats.getErrorRate() < 0.05f) {
                transitionState(UARTState::RUNNING);
            }
            // Full disconnect if no frames for 5 seconds
            if (timeElapsed(_lastValidFrameTime, 5000)) {
                transitionState(UARTState::DISCONNECTED);
            }
            break;
    }
}

const char* LD2412::getUARTStateString() const {
    switch (_uartState) {
        case UARTState::DISCONNECTED: return "DISCONNECTED";
        case UARTState::WAITING_SYNC: return "WAITING_SYNC";
        case UARTState::SYNCED: return "SYNCED";
        case UARTState::RUNNING: return "RUNNING";
        case UARTState::DEGRADED: return "DEGRADED";
        default: return "UNKNOWN";
    }
}

uint8_t LD2412::getHealthScore() const {
    int score = 100;

    // Check last update time
    unsigned long now = millis();
    if (now - _lastValidFrameTime > 1000) {
        int staleSec = (now - _lastValidFrameTime) / 1000;
        score -= (staleSec > 50 ? 50 : staleSec);
    }

    // Check error rate (ratio-based, not absolute)
    float errorRate = _stats.getErrorRate();
    if (errorRate > 0.01f) {
        score -= (int)(errorRate * 100.0f);
    }

    // Use consecutive errors (resets on success) instead of accumulated counters
    score -= (_consecutiveErrors * 5);

    // UART state penalty
    if (_uartState == UARTState::DEGRADED) score -= 20;
    else if (_uartState == UARTState::DISCONNECTED) score -= 50;

    if (score < 0) score = 0;
    if (score > 100) score = 100;

    return (uint8_t)score;
}

bool LD2412::isConnected() const {
    return (_uartState == UARTState::RUNNING || _uartState == UARTState::DEGRADED) &&
           !timeElapsed(_lastValidFrameTime, 3000);
}

// =============================================================================
// Frame Validation (TASK-002)
// =============================================================================

bool LD2412::validateFrame(uint8_t* frame, uint16_t len) {
    if (len < 10) return false; // Minimum valid frame size

    // Verify header (memcmp — cleaner than byte-by-byte)
    if (memcmp(frame, DATA_HEADER, 4) != 0) {
        _stats.checksumErrors++;
        return false;
    }

    // Verify footer
    if (memcmp(frame + len - 4, DATA_FOOTER, 4) != 0) {
        _stats.checksumErrors++;
        return false;
    }

    // Verify length field matches actual length
    uint16_t declaredLen = frame[4] | (frame[5] << 8);
    if (declaredLen != len - 10) { // len - header(4) - length(2) - footer(4)
        _stats.checksumErrors++;
        return false;
    }

    return true;
}

// =============================================================================
// Improved Serial Reading with Ring Buffer (TASK-001, TASK-003)
// =============================================================================

bool LD2412::readSerialImproved() {
    unsigned long now = millis();
    bool gotFrame = false;

    // Drain hardware serial into ring buffer (batch read — fewer syscalls)
    {
        uint8_t tmpBuf[64];
        int avail;
        while ((avail = serial.available()) > 0) {
            size_t toRead = (avail > (int)sizeof(tmpBuf)) ? sizeof(tmpBuf) : (size_t)avail;
            size_t got = serial.readBytes(tmpBuf, toRead);
            if (got > 0) {
                ringPushBatch(tmpBuf, got);
                _stats.bytesReceived += got;
            }
        }
    }

    // Update state machine
    updateUARTState();

    // Process ring buffer
    uint8_t byte;
    const int bufferSize = _engineeringMode ? engSerialBuffer_SIZE : serialBuffer_SIZE;
    const uint16_t maxPayloadLen = (bufferSize > 10) ? (uint16_t)(bufferSize - 10) : 0;
    uint8_t* targetBuffer = _engineeringMode ? engSerialBuffer : serialBuffer;

    while (ringPop(byte)) {
        switch (_parseState) {
            case ParseState::WAIT_HEADER:
                if (byte == DATA_HEADER[_headerIndex]) {
                    targetBuffer[_headerIndex] = byte;
                    _headerIndex++;
                    if (_headerIndex == 4) {
                        _parseState = ParseState::READ_LENGTH;
                        _dataIndex = 4;
                        if (_uartState == UARTState::WAITING_SYNC) {
                            transitionState(UARTState::SYNCED);
                        }
                    }
                } else {
                    // Resync - check if this byte starts a new header
                    if (byte == DATA_HEADER[0]) {
                        targetBuffer[0] = byte;
                        _headerIndex = 1;
                    } else {
                        _headerIndex = 0;
                        _stats.resyncCount++;  // Count garbage bytes during header search
                    }
                }
                break;

            case ParseState::READ_LENGTH:
                if (_dataIndex >= bufferSize) {
                    _parseState = ParseState::WAIT_HEADER;
                    _headerIndex = 0;
                    _stats.invalidFrames++;
                    _consecutiveErrors++;
                    break;
                }
                targetBuffer[_dataIndex++] = byte;
                if (_dataIndex == 6) {
                    _expectedLen = targetBuffer[4] | (targetBuffer[5] << 8);
                    // Sanity check length against buffer limits
                    if (_expectedLen < 5 || _expectedLen > maxPayloadLen) {
                        // Invalid length, resync
                        _parseState = ParseState::WAIT_HEADER;
                        _headerIndex = 0;
                        _stats.invalidFrames++;
                        _consecutiveErrors++;
                    } else {
                        _parseState = ParseState::READ_DATA;
                    }
                }
                break;

            case ParseState::READ_DATA:
                if (_dataIndex >= bufferSize) {
                    _parseState = ParseState::WAIT_HEADER;
                    _headerIndex = 0;
                    _stats.invalidFrames++;
                    _consecutiveErrors++;
                    break;
                }
                targetBuffer[_dataIndex++] = byte;
                // Check if we have all data (header + length + data + footer)
                // FIX #13: Verify footer immediately when last byte received (no extra byte needed)
                if (_dataIndex < _expectedLen + 10) {
                    break;
                }
                _dataIndex = 0;
                // Fall through directly to footer verification
                // [[fallthrough]]

            case ParseState::VERIFY_FOOTER:
                {
                    uint16_t totalLen = _expectedLen + 10;
                    
                    // Final safety check
                    if (totalLen > bufferSize) {
                         _parseState = ParseState::WAIT_HEADER;
                         _headerIndex = 0;
                         _stats.invalidFrames++;
                         break;
                    }
                    
                    bool footerOk = true;
                    for (int i = 0; i < 4; i++) {
                        if (targetBuffer[totalLen - 4 + i] != DATA_FOOTER[i]) {
                            footerOk = false;
                            break;
                        }
                    }

                    if (footerOk && validateFrame(targetBuffer, totalLen)) {
                        // Valid frame!
                        _stats.validFrames++;
                        _consecutiveErrors = 0;

                        // Update frame rate (exponential moving average)
                        // FIX #18: Guard div/0 when two frames arrive in same millis() tick
                        unsigned long frameNow = millis();
                        unsigned long frameDelta = frameNow - _lastValidFrameTime;
                        if (_lastValidFrameTime > 0 && frameDelta > 0 && !timeElapsed(_lastValidFrameTime, 1000)) {
                            float instantRate = 1000.0f / (float)frameDelta;
                            if (instantRate > 50.0f) instantRate = 50.0f; // Sanity cap: LD2412 max ~10Hz
                            _stats.frameRate = 0.9f * _stats.frameRate + 0.1f * instantRate;
                        }
                        _lastValidFrameTime = frameNow;

                        // Transition to RUNNING if we were syncing
                        if (_uartState == UARTState::SYNCED || _uartState == UARTState::WAITING_SYNC) {
                            transitionState(UARTState::RUNNING);
                        }

                        // Process engineering data if applicable
                        if (_engineeringMode) {
                            if (totalLen >= engSerialBuffer_SIZE) {
                                // Full engineering frame
                                if (engSerialBuffer[DATA_TYPE_OFFSET] == 0x01) {
                                    for (int g = 0; g < 14; g++) {
                                        _gateMovingEnergy[g] = engSerialBuffer[MOVING_GATE_START + g];
                                        _gateStillEnergy[g] = engSerialBuffer[STILL_GATE_START + g];
                                    }
                                    _lightLevel = engSerialBuffer[LIGHT_SENSOR_OFFSET];
                                }
                            }
                            // Always copy basic data to serialBuffer for targetState()/distance()/energy()
                            uint16_t copyLen = (totalLen < serialBuffer_SIZE) ? totalLen : serialBuffer_SIZE;
                            for (int j = 0; j < copyLen; j++) {
                                serialBuffer[j] = engSerialBuffer[j];
                            }
                        }

                        // Diagnostic: log frame data type periodically
                        {
                            static unsigned long lastTypeLog = 0;
                            if (millis() - lastTypeLog > 5000) {
                                lastTypeLog = millis();
                                uint8_t dataType = targetBuffer[6]; // 0x01=eng, 0x02=basic
                                Serial.printf("[LD2412] Frame: len=%d type=0x%02X (expect %s)\n",
                                              totalLen, dataType, _engineeringMode ? "ENG" : "BASIC");
                            }
                        }

                        // Detect engineering mode loss (receiving basic frames when eng mode expected)
                        if (_engineeringMode) {
                            if (totalLen < engSerialBuffer_SIZE) {
                                _engLostCount++;
                                if (_engLostCount >= 5) {
                                    _engModeLost = true;
                                }
                            } else {
                                _engLostCount = 0;
                                _engModeLost = false;
                            }
                        }

                        serialLastRead = frameNow;
                        _parseState = ParseState::WAIT_HEADER;
                        _headerIndex = 0;
                        gotFrame = true;
                        break;
                    } else {
                        // Invalid frame
                        _stats.invalidFrames++;
                        _consecutiveErrors++;
                    }
                }
                _parseState = ParseState::WAIT_HEADER;
                _headerIndex = 0;
                break;
        }
    }

    return gotFrame;
}

// Keep original for backward compatibility but use improved version
bool LD2412::readSerial() {
    // Check refresh threshold (millis overflow safe - TASK-007)
    if (serialLastRead != 0 && !timeElapsed(serialLastRead, refresh_threshold)) {
        return true;
    }

    return readSerialImproved();
}

// =============================================================================
// Command Functions (TASK-003 - removed blocking delay)
// =============================================================================

void LD2412::sendCommand(uint8_t* data, uint8_t len) {
    this->data_len[0] = len;

    this->serial.write(FRAME_HEADER, 4);
    this->serial.write(this->data_len, 2);
    this->serial.write(data, this->data_len[0]);
    this->serial.write(FRAME_FOOTER, 4);

    this->serial.flush();
}

// Non-blocking ACK reader (TASK-003)
uint8_t* LD2412::getAckNonBlocking(uint8_t respData, uint8_t len, unsigned long timeout) {
    unsigned long startTime = millis();
    int i = 0;

    // Wait for data without blocking delay
    while (!timeElapsed(startTime, timeout)) {
        if (serial.available()) {
            break;
        }
        yield(); // Allow other tasks to run
    }

    while (serial.available() && i < len) {
        for (i = 0; i < len; i++) {
            // Wait for next byte with timeout
            unsigned long byteStart = millis();
            while (!serial.available()) {
                if (timeElapsed(byteStart, 50)) { // 50ms per-byte timeout
                    return nullptr;
                }
                yield();
            }

            this->buffer[i] = serial.read();

            // Ensure packet capture is properly aligned at header
            if (this->buffer[0] != FRAME_HEADER[0]
                || (i > 0 && this->buffer[1] != FRAME_HEADER[1])
                || (i > 1 && this->buffer[2] != FRAME_HEADER[2])
                || (i > 2 && this->buffer[3] != FRAME_HEADER[3])) {
                i = -1;
                continue;
            }

            // Early length validation: after reading length field (bytes 4-5),
            // check declared payload matches expected frame size (ESPHome #14297 analogue)
            if (i == 5) {
                uint16_t declaredLen = this->buffer[4] | (this->buffer[5] << 8);
                if (declaredLen != (uint16_t)(len - 10)) {
                    _stats.invalidFrames++;
                    return nullptr;
                }
            }

            // Overall timeout (TASK-007 - overflow safe)
            if (timeElapsed(startTime, timeout)) {
                _stats.timeouts++;
                return nullptr;
            }
        }
    }

    if (i >= len) {
        // Verify frame structure
        for (i = 0; i < len; i++) {
            if ((i < 4 && this->buffer[i] != FRAME_HEADER[i])
                || (i == 4 && this->buffer[i] != len - 10)
                || (i == 5 && this->buffer[i] != 0x00)
                || (i == 6 && this->buffer[i] != respData)
                || (i == 7 && this->buffer[i] != 0x01)
                || (i >= len - 4 && this->buffer[i] != FRAME_FOOTER[i - (len - 4)])) {
                _stats.invalidFrames++;
                return nullptr;
            }
        }
        _stats.validFrames++;
        return this->buffer;
    }
    return nullptr;
}

// Updated getAck using non-blocking version
uint8_t* LD2412::getAck(uint8_t respData, uint8_t len) {
    return getAckNonBlocking(respData, len, ACK_TIMEOUT);
}

bool LD2412::enableConfig() {
    // Drain stale UART data before sending command (prevents misparse on retry)
    while (serial.available()) serial.read();
    uint8_t data[] = {0xFF, 0x00, 0x01, 0x00};
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 18); ack != nullptr && ack[8] == 0x00)
        return true;
    return false;
}

bool LD2412::disableConfig() {
    uint8_t data[] = {0xFE, 0x00};
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 14); ack != nullptr && ack[8] == 0x00)
        return true;
    return false;
}

// =============================================================================
// Dynamic Background Correction & System Commands
// =============================================================================

bool LD2412::startDynamicBackgroundCorrection() {
    uint8_t data[] = {0x0B, 0x00};
    bool success = false;

    if (!enableConfig())
        return false;

    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 14); ack != nullptr && ack[8] == 0x00)
        success = true;
    disableConfig();
    return success;
}

int LD2412::queryDynamicBackgroundCorrection() {
    uint8_t data[] = {0x1B, 0x00};

    if (!enableConfig())
        if (!enableConfig())
            return -1;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 16); ack != nullptr && ack[8] == 0x00) {
        uint8_t temp = ack[10];
        disableConfig();
        return temp;
    }
    disableConfig();
    return -1;
}

int* LD2412::readFirmwareVersion() {
    uint8_t data[] = {0xA0, 0x00};

    if (!enableConfig())
        if (!enableConfig())
            return nullptr;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 22); ack != nullptr && ack[8] == 0x00) {
        this->firmwareResponse[0] = ack[10] + (ack[11] << 8);
        this->firmwareResponse[1] = ack[12] + (ack[13] << 8);
        this->firmwareResponse[2] = ack[14] + (ack[15] << 8) + (ack[16] << 16) + (ack[17] << 24);
        disableConfig();
        return this->firmwareResponse;
    }
    disableConfig();
    return nullptr;
}

bool LD2412::resetDeviceSettings() {
    uint8_t data[] = {0xA2, 0x00};
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 14); ack != nullptr && ack[8] == 0x00)
        success = true;
    disableConfig();
    return success;
}

bool LD2412::restartModule() {
    uint8_t data[] = {0xA3, 0x00};
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 14); ack != nullptr && ack[8] == 0x00)
        success = true;
    disableConfig();
    return success;
}

bool LD2412::setBluetooth(bool enable) {
    uint8_t data[] = {0xA4, 0x00, enable ? (uint8_t)0x01 : (uint8_t)0x00, 0x00};
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 14); ack != nullptr && ack[8] == 0x00)
        success = true;
    disableConfig();
    return success;
}

bool LD2412::readMacAddress(uint8_t mac[6]) {
    uint8_t data[] = {0xA5, 0x00, 0x01, 0x00};
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;
    sendCommand(data, sizeof(data));

    // ACK layout: 4 hdr + 2 len + 2 cmd-echo + 2 status + 6 MAC + 4 footer = 20 bytes
    if (const uint8_t* ack = getAck(data[0], 20); ack != nullptr && ack[8] == 0x00) {
        for (uint8_t i = 0; i < 6; i++) mac[i] = ack[10 + i];
        success = true;
    }
    disableConfig();
    return success;
}

// =============================================================================
// Configuration SET Functions
// =============================================================================

bool LD2412::setParamConfig(uint8_t min, uint8_t max, uint8_t duration, uint8_t outPinPolarity) {
    uint8_t data[] = {0x02, 0x00, min, max, duration, 0x00, outPinPolarity};
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 14); ack != nullptr && ack[8] == 0x00)
        success = true;
    disableConfig();
    return success;
}

bool LD2412::setMotionSensitivity(uint8_t sen) {
    uint8_t data[16] = {0x03, 0x00};
    for (int i = 2; i < 16; i++)
        data[i] = sen;
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 14); ack != nullptr && ack[8] == 0x00)
        success = true;
    disableConfig();
    return success;
}

bool LD2412::setMotionSensitivity(uint8_t sen[14]) {
    uint8_t data[16] = {0x03, 0x00};
    for (int i = 2; i < 16; i++)
        data[i] = sen[i - 2];
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 14); ack != nullptr && ack[8] == 0x00)
        success = true;
    disableConfig();
    return success;
}

bool LD2412::setStaticSensitivity(uint8_t sen) {
    uint8_t data[16] = {0x04, 0x00};
    for (int i = 2; i < 16; i++)
        data[i] = sen;
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 14); ack != nullptr && ack[8] == 0x00)
        success = true;
    disableConfig();
    return success;
}

bool LD2412::setStaticSensitivity(uint8_t sen[14]) {
    uint8_t data[16] = {0x04, 0x00};
    for (int i = 2; i < 16; i++)
        data[i] = sen[i - 2];
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 14); ack != nullptr && ack[8] == 0x00)
        success = true;
    disableConfig();
    return success;
}

bool LD2412::setBaudRate(int baud) {
    uint8_t data[] = {0xA1, 0x00, 0x05, 0x00};
    switch (baud) {
        case 9600:    data[2] = 0x01; break;
        case 19200:   data[2] = 0x02; break;
        case 38400:   data[2] = 0x03; break;
        case 57600:   data[2] = 0x04; break;
        case 115200:  data[2] = 0x05; break;
        case 230400:  data[2] = 0x06; break;
        case 256000:  data[2] = 0x07; break;
        case 460800:  data[2] = 0x08; break;
        default:      return false;
    }
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 14); ack != nullptr && ack[8] == 0x00)
        success = true;
    disableConfig();
    return success;
}

bool LD2412::setResolution(uint8_t mode) {
    // ESPHome-compatible resolution command
    // mode: 0=0.75m, 1=0.50m, 3=0.20m (NOT 2!)
    if (mode != 0 && mode != 1 && mode != 3) {
        return false;
    }

    // Command 0x01 with 6-byte value payload (ESPHome format)
    uint8_t data[] = {0x01, 0x00, mode, 0x00, 0x00, 0x00, 0x00, 0x00};
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;

    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(0x01, 14); ack != nullptr && ack[8] == 0x00)
        success = true;

    disableConfig();

    // Resolution change requires radar restart (per ESPHome)
    if (success) {
        delay(200);
        restartModule();
    }
    return success;
}

void LD2412::setSerialRefreshThres(unsigned int refreshTime) {
    this->refresh_threshold = refreshTime;
}

// =============================================================================
// Light Sensor Configuration (Task #11)
// =============================================================================

bool LD2412::setLightFunction(uint8_t mode) {
    // mode: 0=OFF, 1=below threshold (night), 2=above threshold (day)
    if (mode > 2) return false;

    uint8_t data[] = {0x06, 0x00, mode, 0x00};
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 14); ack != nullptr && ack[8] == 0x00)
        success = true;
    disableConfig();
    return success;
}

bool LD2412::setLightThreshold(uint8_t threshold) {
    uint8_t data[] = {0x07, 0x00, threshold, 0x00};
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 14); ack != nullptr && ack[8] == 0x00)
        success = true;
    disableConfig();
    return success;
}

int LD2412::getLightFunction() {
    uint8_t data[] = {0x16, 0x00};

    if (!enableConfig())
        if (!enableConfig())
            return -1;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 15); ack != nullptr && ack[8] == 0x00) {
        int mode = ack[10];
        disableConfig();
        return mode;
    }
    disableConfig();
    return -1;
}

int LD2412::getLightThreshold() {
    uint8_t data[] = {0x17, 0x00};

    if (!enableConfig())
        if (!enableConfig())
            return -1;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 15); ack != nullptr && ack[8] == 0x00) {
        int threshold = ack[10];
        disableConfig();
        return threshold;
    }
    disableConfig();
    return -1;
}

int LD2412::getResolution() {
    // Query resolution mode: cmd 0x11 (ESPHome: CMD_QUERY_DISTANCE_RESOLUTION)
    // Pattern: query = set_cmd + 0x10 (set=0x01 → query=0x11)
    uint8_t data[] = {0x11, 0x00};

    if (!enableConfig())
        if (!enableConfig())
            return -1;
    sendCommand(data, sizeof(data));

    // Response: HEADER(4)+LEN(2)+CMD(2)+STATUS(2)+VALUE(6)+FOOTER(4) = 20 bytes
    if (const uint8_t* ack = getAck(data[0], 20); ack != nullptr && ack[8] == 0x00) {
        int mode = ack[10];  // 0=0.75m, 1=0.50m, 3=0.20m
        disableConfig();
        return mode;
    }
    disableConfig();
    return -1;
}

// =============================================================================
// Configuration GET Functions
// =============================================================================

int* LD2412::getParamConfig() {
    uint8_t data[] = {0x12, 0x00};

    if (!enableConfig())
        if (!enableConfig())
            return nullptr;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 19); ack != nullptr && ack[8] == 0x00) {
        for (int i = 10; i < 15; i++)
            this->paramResponse[i - 10] = static_cast<int>(ack[i]);
        disableConfig();
        return this->paramResponse;
    }
    disableConfig();
    return nullptr;
}

int LD2412::getMotionSensitivity() {
    uint8_t data[] = {0x13, 0x00};

    if (!enableConfig())
        if (!enableConfig())
            return -1;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 28); ack != nullptr && ack[8] == 0x00) {
        int min = 100;
        for (int i = 10; i < 24; i++)
            if (ack[i] < min)
                min = ack[i];
        disableConfig();
        return min;
    }
    disableConfig();
    return -1;
}

int* LD2412::getMotionSensitivity(std::true_type) {
    uint8_t data[] = {0x13, 0x00};

    if (!enableConfig())
        if (!enableConfig())
            return nullptr;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 28); ack != nullptr && ack[8] == 0x00) {
        for (int i = 10; i < 24; i++)
            this->sensResponse[i - 10] = static_cast<int>(ack[i]);
        disableConfig();
        return this->sensResponse;
    }
    disableConfig();
    return nullptr;
}

int LD2412::getStaticSensitivity() {
    uint8_t data[] = {0x14, 0x00};

    if (!enableConfig())
        if (!enableConfig())
            return -1;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 28); ack != nullptr && ack[8] == 0x00) {
        int min = 100;
        for (int i = 10; i < 24; i++)
            if (ack[i] < min)
                min = ack[i];
        disableConfig();
        return min;
    }
    disableConfig();
    return -1;
}

int* LD2412::getStaticSensitivity(std::true_type) {
    uint8_t data[] = {0x14, 0x00};

    if (!enableConfig())
        if (!enableConfig())
            return nullptr;
    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(data[0], 28); ack != nullptr && ack[8] == 0x00) {
        for (int i = 10; i < 24; i++)
            this->sensResponse[i - 10] = static_cast<int>(ack[i]);
        disableConfig();
        return this->sensResponse;
    }
    disableConfig();
    return nullptr;
}

unsigned int LD2412::getSerialRefreshThres() {
    return this->refresh_threshold;
}

// =============================================================================
// Real-time Data Functions
// =============================================================================

int LD2412::targetState() {
    if (!readSerial())
        return -1;
    return this->serialBuffer[8];
}

int LD2412::movingDistance() {
    if (!readSerial())
        return -1;
    return this->serialBuffer[9] + (this->serialBuffer[10] << 8);
}

int LD2412::movingEnergy() {
    if (!readSerial())
        return -1;
    return this->serialBuffer[11];
}

int LD2412::staticDistance() {
    if (!readSerial())
        return -1;
    return this->serialBuffer[12] + (this->serialBuffer[13] << 8);
}

int LD2412::staticEnergy() {
    if (!readSerial())
        return -1;
    return this->serialBuffer[14];
}

RadarSnapshot LD2412::readSnapshot() {
    RadarSnapshot snap;
    if (!readSerial()) {
        snap.valid = false;
        snap.state = -1;
        snap.movingDistance = 0;
        snap.movingEnergy = 0;
        snap.staticDistance = 0;
        snap.staticEnergy = 0;
        return snap;
    }
    // All reads from the same serialBuffer — no second readSerial() call
    snap.valid = true;
    snap.state = this->serialBuffer[8];
    snap.movingDistance = this->serialBuffer[9] + (this->serialBuffer[10] << 8);
    snap.movingEnergy = this->serialBuffer[11];
    snap.staticDistance = this->serialBuffer[12] + (this->serialBuffer[13] << 8);
    snap.staticEnergy = this->serialBuffer[14];
    return snap;
}

// =============================================================================
// Engineering Mode
// =============================================================================

bool LD2412::enableTrackingMode(bool enable) {
    // Experimental command for tracking mode
    uint8_t cmd = enable ? 0x80 : 0x81; 
    uint8_t data[] = {cmd, 0x00};
    bool success = false;

    if (!enableConfig())
        if (!enableConfig())
            return false;

    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(cmd, 14); ack != nullptr && ack[8] == 0x00)
        success = true;

    disableConfig();
    return success;
}

bool LD2412::setEngineeringMode(bool enable) {
    uint8_t cmd = enable ? 0x62 : 0x63;
    uint8_t data[] = {cmd, 0x00};
    bool success = false;

    // Drain pending data bytes before command sequence
    while (serial.available()) { serial.read(); }
    delay(50);
    while (serial.available()) { serial.read(); }

    if (!enableConfig())
        if (!enableConfig())
            return false;

    sendCommand(data, sizeof(data));

    if (const uint8_t* ack = getAck(cmd, 14); ack != nullptr) {
        Serial.printf("[LD2412] Eng ACK: %02X %02X %02X %02X | status=%02X\n",
                      ack[6], ack[7], ack[8], ack[9], ack[8]);
        if (ack[8] == 0x00)
            success = true;
    } else {
        Serial.println("[LD2412] Eng ACK: nullptr (timeout/NACK)");
    }

    disableConfig();

    if (success) {
        _engineeringMode = enable;
    }
    return success;
}

int LD2412::getMovingGateEnergy(uint8_t gate) {
    if (!_engineeringMode || gate > 13) return -1;
    return _gateMovingEnergy[gate];
}

int LD2412::getStillGateEnergy(uint8_t gate) {
    if (!_engineeringMode || gate > 13) return -1;
    return _gateStillEnergy[gate];
}

int LD2412::getLightLevel() {
    if (!_engineeringMode) return -1;
    return _lightLevel;
}
