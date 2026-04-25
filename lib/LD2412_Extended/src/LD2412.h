/**
 * @file LD2412.h
 * @author Trent Tobias (original)
 * @see Upstream: https://github.com/tobiastl/LD2412 (MIT, Copyright (c) 2025 tobiastl)
 * @version 2.0.0
 * @date January 27, 2026
 * @brief LD2412 serial communication - Enhanced for 24/7 security systems
 *
 * Changes in v2.0.0:
 * - Added ring buffer for UART data (TASK-001)
 * - Added UART state machine (TASK-004)
 * - Added statistics collection (TASK-008)
 * - Removed blocking delay from getAck (TASK-003)
 * - Added frame checksum validation (TASK-002)
 * - Fixed millis() overflow handling (TASK-007)
 */

#ifndef LD2412_H
#define LD2412_H

#include <Arduino.h>
#include <type_traits>

#define RETURN_ARRAY (std::true_type{})

// Atomic snapshot of radar data (one consistent frame)
struct RadarSnapshot {
    int state;           // 0=none, 1=moving, 2=static, 3=both, -1=invalid
    int movingDistance;   // cm
    int movingEnergy;     // 0-100
    int staticDistance;   // cm
    int staticEnergy;     // 0-100
    bool valid;           // true if readSerial succeeded
};

// UART State Machine states
enum class UARTState : uint8_t {
    DISCONNECTED,   // No UART activity for extended period
    WAITING_SYNC,   // Scanning for valid frame header
    SYNCED,         // Header found, reading frame data
    RUNNING,        // Valid frames being received normally
    DEGRADED        // High error rate detected
};

// Frame parsing states
enum class ParseState : uint8_t {
    WAIT_HEADER,
    READ_LENGTH,
    READ_DATA,
    VERIFY_FOOTER
};

// Resolution modes (gate distance)
enum class ResolutionMode : uint8_t {
    RES_0_75M = 0,  // 0.75m per gate, range 0-9m (default)
    RES_0_50M = 1,  // 0.50m per gate, range 0-6.5m
    RES_0_20M = 3   // 0.20m per gate, range 0-2.6m (ESPHome: 0x03)
};

// Light function modes (OUT pin behavior based on light)
enum class LightFunction : uint8_t {
    OFF = 0x00,           // Light sensor disabled
    BELOW_THRESHOLD = 0x01, // OUT active when light < threshold (night mode)
    ABOVE_THRESHOLD = 0x02  // OUT active when light > threshold (day mode)
};

// Statistics structure for monitoring
struct UARTStatistics {
    uint32_t bytesReceived = 0;
    uint32_t validFrames = 0;
    uint32_t invalidFrames = 0;
    uint32_t checksumErrors = 0;
    uint32_t timeouts = 0;
    uint32_t resyncCount = 0;
    uint32_t bufferOverflows = 0;
    unsigned long lastFrameTime = 0;
    float frameRate = 0.0f;

    void reset() {
        bytesReceived = 0;
        validFrames = 0;
        invalidFrames = 0;
        checksumErrors = 0;
        timeouts = 0;
        resyncCount = 0;
        bufferOverflows = 0;
        lastFrameTime = 0;
        frameRate = 0.0f;
    }

    float getErrorRate() const {
        uint32_t total = validFrames + invalidFrames;
        return total > 0 ? (float)invalidFrames / (float)total : 0.0f;
    }
};

class LD2412 {

public:
    /**
     * @brief Constructor
     * @param ld_serial HardwareSerial or SoftwareSerial object reference
     */
    LD2412(Stream& ld_serial);

    // --- Statistics & Health ---

    /**
     * @brief Get current UART statistics
     */
    const UARTStatistics& getStatistics() const { return _stats; }

    /**
     * @brief Reset statistics counters
     */
    void resetStatistics() { _stats.reset(); }

    /**
     * @brief Get current UART state
     */
    UARTState getUARTState() const { return _uartState; }

    /**
     * @brief Get state as string for debugging
     */
    const char* getUARTStateString() const;

    /**
     * @brief Calculate health score (0-100)
     */
    uint8_t getHealthScore() const;

    /**
     * @brief Check if radar is connected (receiving valid frames)
     */
    bool isConnected() const;

    // --- Dynamic Background Correction (DBC) ---
    // Also known upstream as "calibration mode". HLK datasheet / ESPHome name:
    // "start_dynamic_background_correction". Command word 0x0B (start), 0x1B (query).
    // Duration ~120 s per HLK datasheet — room must stay static during the procedure.

    /**
     * @brief Start dynamic background correction (cmd 0x0B).
     *        Takes ~120 s; radar learns static reflections in the scene.
     */
    bool startDynamicBackgroundCorrection();

    /**
     * @brief Check DBC status (cmd 0x1B).
     * @return 1 if DBC in progress, 0 if idle, -1 if error
     */
    int queryDynamicBackgroundCorrection();

    /**
     * @brief Legacy alias for startDynamicBackgroundCorrection().
     * @deprecated Misnomer — this is DBC, not a 10-second user calibration.
     *             Kept for source compatibility with firmware <= v4.5.6-poe-wifi.
     */
    bool enterCalibrationMode() { return startDynamicBackgroundCorrection(); }

    /**
     * @brief Legacy alias for queryDynamicBackgroundCorrection().
     * @deprecated See enterCalibrationMode().
     */
    int checkCalibrationMode() { return queryDynamicBackgroundCorrection(); }

    // --- System Commands ---

    /**
     * @brief Read firmware version
     * @return Array: [0] type, [1] major, [2] minor. nullptr on failure.
     */
    int* readFirmwareVersion();

    /**
     * @brief Read radar MAC address (cmd 0xA5).
     * @param mac Output buffer, 6 bytes.
     * @return true on success. Sentinel MAC 01:02:03:04:05:08 means Bluetooth
     *         is disabled on the radar (per HLK protocol / ESPHome convention).
     */
    bool readMacAddress(uint8_t mac[6]);

    /**
     * @brief Enable or disable radar's Bluetooth (HLK app pairing).
     *        cmd 0xA4, payload {enable, 0x00}. Takes effect after restartModule().
     * @param enable true = BT on, false = BT off (recommended for deployed units).
     */
    bool setBluetooth(bool enable);

    /**
     * @brief Factory reset
     */
    bool resetDeviceSettings();

    /**
     * @brief Restart radar module
     */
    bool restartModule();

    // --- Configuration ---

    /**
     * @brief Set detection parameters
     * @param min Minimum gate (1-14)
     * @param max Maximum gate (1-14)
     * @param duration Unmanned duration (seconds)
     * @param outPinPolarity OUT pin polarity
     */
    bool setParamConfig(uint8_t min, uint8_t max, uint8_t duration, uint8_t outPinPolarity);

    /**
     * @brief Set motion sensitivity (all gates)
     */
    bool setMotionSensitivity(uint8_t sen);

    /**
     * @brief Set motion sensitivity (per gate)
     */
    bool setMotionSensitivity(uint8_t sen[14]);

    /**
     * @brief Set static sensitivity (all gates)
     */
    bool setStaticSensitivity(uint8_t sen);

    /**
     * @brief Set static sensitivity (per gate)
     */
    bool setStaticSensitivity(uint8_t sen[14]);

    /**
     * @brief Set baud rate
     */
    bool setBaudRate(int baud);

    /**
     * @brief Set range resolution (gate distance)
     * @param mode Resolution mode:
     *        - 0 (RES_0_75M): 0.75m/gate, range 0-9m (default)
     *        - 1 (RES_0_50M): 0.50m/gate, range 0-6.5m
     *        - 2 (RES_0_20M): 0.20m/gate, range 0-2.6m
     * @return true if successful
     */
    bool setResolution(uint8_t mode);
    bool setResolution(ResolutionMode mode) { return setResolution(static_cast<uint8_t>(mode)); }

    /**
     * @brief Set serial refresh threshold (ms)
     */
    void setSerialRefreshThres(unsigned int refreshTime);

    /**
     * @brief Get current resolution mode
     * @return 0=0.75m, 1=0.50m, 2=0.20m, -1=error
     */
    int getResolution();

    // --- Light Sensor Configuration ---

    /**
     * @brief Set light function mode
     * @param mode 0=off, 1=below threshold (night), 2=above threshold (day)
     */
    bool setLightFunction(uint8_t mode);
    bool setLightFunction(LightFunction mode) { return setLightFunction(static_cast<uint8_t>(mode)); }

    /**
     * @brief Set light threshold (0-255)
     * @param threshold Light level threshold for OUT pin activation
     */
    bool setLightThreshold(uint8_t threshold);

    /**
     * @brief Get current light function mode
     * @return 0=off, 1=below, 2=above, -1=error
     */
    int getLightFunction();

    /**
     * @brief Get current light threshold
     * @return 0-255, -1=error
     */
    int getLightThreshold();

    // --- Query Configuration ---

    /**
     * @brief Read current parameters
     * @return Array: [0] success, [1] min, [2] max, [3] duration, [4] polarity
     */
    int* getParamConfig();

    /**
     * @brief Get minimum motion sensitivity across all gates
     */
    int getMotionSensitivity();

    /**
     * @brief Get per-gate motion sensitivity array
     */
    int* getMotionSensitivity(std::true_type);

    /**
     * @brief Get minimum static sensitivity across all gates
     */
    int getStaticSensitivity();

    /**
     * @brief Get per-gate static sensitivity array
     */
    int* getStaticSensitivity(std::true_type);

    /**
     * @brief Get serial refresh threshold
     */
    unsigned int getSerialRefreshThres();

    // --- Real-time Data ---

    /**
     * @brief Get target state (0=none, 1=moving, 2=static, 3=both)
     * @return State or -1 on failure
     */
    int targetState();

    /**
     * @brief Get moving target distance (cm)
     */
    int movingDistance();

    /**
     * @brief Get moving target energy (0-100)
     */
    int movingEnergy();

    /**
     * @brief Get static target distance (cm)
     */
    int staticDistance();

    /**
     * @brief Get static target energy (0-100)
     */
    int staticEnergy();

    /**
     * @brief Atomic read of all radar data from a single frame
     * @return RadarSnapshot with all fields from one consistent frame
     */
    RadarSnapshot readSnapshot();

    // --- Engineering Mode ---

    /**
     * @brief Enable Tracking Mode (Multi-target if supported)
     */
    bool enableTrackingMode(bool enable);

    /**
     * @brief Enable/disable engineering mode
     */
    bool setEngineeringMode(bool enable);

    /**
     * @brief Check if engineering mode is active
     */
    bool isEngineeringMode() const { return _engineeringMode; }

    /**
     * @brief Check if engineering mode was lost (radar sending basic frames)
     */
    bool isEngModeLost() const { return _engModeLost; }

    /**
     * @brief Clear engineering mode lost flag (after successful re-enable)
     */
    void clearEngModeLost() { _engModeLost = false; _engLostCount = 0; }

    /**
     * @brief Get moving energy for specific gate
     */
    int getMovingGateEnergy(uint8_t gate);

    /**
     * @brief Get static energy for specific gate
     */
    int getStillGateEnergy(uint8_t gate);

    /**
     * @brief Get light sensor value (0-255)
     */
    int getLightLevel();

    /**
     * @brief Get all moving energies array
     */
    const uint8_t* getAllMovingEnergies() const { return _gateMovingEnergy; }

    /**
     * @brief Get all static energies array
     */
    const uint8_t* getAllStillEnergies() const { return _gateStillEnergy; }

private:
    // --- Ring Buffer (Thread-safe pro ESP32 multi-core) ---
    static constexpr uint16_t RING_BUFFER_SIZE = 256;
    uint8_t _ringBuffer[RING_BUFFER_SIZE];
    volatile uint16_t _ringHead = 0;
    volatile uint16_t _ringTail = 0;
    mutable portMUX_TYPE _ringMux = portMUX_INITIALIZER_UNLOCKED;

    void ringPush(uint8_t byte);
    void ringPushBatch(const uint8_t* data, size_t len);
    bool ringPop(uint8_t& byte);
    uint16_t ringAvailable() const;
    void ringClear();

    // --- Frame Parsing ---
    ParseState _parseState = ParseState::WAIT_HEADER;
    uint8_t _headerIndex = 0;
    uint16_t _expectedLen = 0;
    uint16_t _dataIndex = 0;

    // --- UART State Machine ---
    UARTState _uartState = UARTState::DISCONNECTED;
    unsigned long _lastValidFrameTime = 0;
    unsigned long _stateEntryTime = 0;
    uint8_t _consecutiveErrors = 0;

    void updateUARTState();
    void transitionState(UARTState newState);

    // --- Timing (millis overflow safe) ---
    static inline bool timeElapsed(unsigned long start, unsigned long interval) {
        return (unsigned long)(millis() - start) >= interval;
    }

    // --- Serial & Buffers ---
    Stream& serial;

    static constexpr int ACK_TIMEOUT = 200;
    static constexpr unsigned int BUFFER_SIZE = 64;
    uint8_t buffer[BUFFER_SIZE];

    int paramResponse[5];
    int sensResponse[14];
    int firmwareResponse[3];

    // Engineering mode data
    bool _engineeringMode = false;
    bool _engModeLost = false;
    uint8_t _engLostCount = 0;
    uint8_t _gateMovingEnergy[14] = {0};
    uint8_t _gateStillEnergy[14] = {0};
    uint8_t _lightLevel = 0;

    // Serial reading
    unsigned int refresh_threshold = 5;
    unsigned long serialLastRead = 0;

    static constexpr int serialBuffer_SIZE = 21;
    uint8_t serialBuffer[serialBuffer_SIZE];

    static constexpr int engSerialBuffer_SIZE = 54;
    uint8_t engSerialBuffer[engSerialBuffer_SIZE];

    // Engineering mode frame offsets
    static constexpr int DATA_TYPE_OFFSET = 6;
    static constexpr int MOVING_GATE_START = 17;
    static constexpr int STILL_GATE_START = 31;
    static constexpr int LIGHT_SENSOR_OFFSET = 45;

    // Frame structure - Command frames
    const uint8_t FRAME_HEADER[4] = {0xFD, 0xFC, 0xFB, 0xFA};
    const uint8_t FRAME_FOOTER[4] = {0x04, 0x03, 0x02, 0x01};

    // Frame structure - Data frames
    const uint8_t DATA_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
    const uint8_t DATA_FOOTER[4] = {0xF8, 0xF7, 0xF6, 0xF5};

    uint8_t data_len[2] = {0x00, 0x00};

    // Statistics
    UARTStatistics _stats;

    // --- Internal Functions ---
    void sendCommand(uint8_t* data, uint8_t len);
    uint8_t* getAck(uint8_t respData, uint8_t len);
    uint8_t* getAckNonBlocking(uint8_t respData, uint8_t len, unsigned long timeout);
    bool enableConfig();
    bool disableConfig();
    bool readSerial();
    bool readSerialImproved();
    bool validateFrame(uint8_t* frame, uint16_t len);
};

#endif //LD2412_H
