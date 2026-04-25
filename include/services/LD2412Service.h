#ifndef LD2412_SERVICE_H
#define LD2412_SERVICE_H

#include <Arduino.h>
#include <atomic>
#include <LD2412.h>
#include <ArduinoJson.h>

enum class PresenceState {
    IDLE,
    PRESENCE_DETECTED,
    HOLD_TIMEOUT,
    TAMPER
};

enum class MotionType {
    NONE,
    STATIONARY,
    MOVING
};

enum class MotionDirection {
    STATIC,
    APPROACHING,
    RECEDING,
    UNKNOWN
};

struct RadarData {
    PresenceState state;
    MotionType motion_type;
    MotionDirection motion_direction;
    uint16_t distance_cm;
    uint8_t moving_energy;
    uint8_t static_energy;
    bool tamper_alert;
    bool valid = true;   // FIX #9: false when data unavailable (mutex timeout)
};

class LD2412Service {
public:
    LD2412Service(int8_t rxPin, int8_t txPin);
    ~LD2412Service();

    bool begin(HardwareSerial &serial, uint8_t minGate = 0, uint8_t maxGate = 13);
    void stop();
    void update();
    RadarData getData() const;

    // Config
    bool setMotionSensitivity(uint8_t sensitivity);
    bool setMotionSensitivity(uint8_t sens[14]);
    bool setStaticSensitivity(uint8_t sensitivity);
    bool setStaticSensitivity(uint8_t sens[14]);
    bool setParamConfig(uint8_t minGate, uint8_t maxGate, uint8_t duration);
    bool setResolution(float resolution);
    int getResolution();
    bool factoryReset();
    bool restartRadar();

    // Light Sensor Configuration (Task #11)
    bool setLightFunction(uint8_t mode);
    bool setLightThreshold(uint8_t threshold);
    int getLightFunction();
    int getLightThreshold();

    void setHoldTime(unsigned long ms);
    unsigned long getHoldTime() const;

    // Calibration
    bool startCalibration();
    int checkCalibrationStatus();

    // Pet Immunity (software filter)
    void setMinMoveEnergy(uint8_t val);
    uint8_t getMinMoveEnergy() const;
    void setTamperDetected(bool tamper);
    bool isTamperDetected() const { return _tamperDetected; }

    // Health - Enhanced (TASK-005, TASK-006, TASK-009)
    bool isRadarConnected() const;
    uint8_t getHealthScore() const;
    const char* getUARTStateString() const;
    float getFrameRate() const;
    uint32_t getErrorCount() const;
    uint32_t getValidFrameCount() const;

    // State change detection (atomic exchange to avoid TOCTOU race)
    bool consumeStateChange() { return _stateChanged.exchange(false); }
    bool hasStateChanged() const { return _stateChanged; }

    // Firmware info
    bool readFirmwareVersion();
    int getFirmwareMajor() const { return _fwMajor; }
    int getFirmwareMinor() const { return _fwMinor; }

    // Configuration Cache (TASK-012)
    void loadConfigFromRadar();
    const uint8_t* getMotionSensitivityArray() const { return _motionSensitivity; }
    const uint8_t* getStaticSensitivityArray() const { return _staticSensitivity; }
    uint8_t getMinGate() const { return _minGate; }
    uint8_t getMaxGate() const { return _maxGate; }
    uint8_t getMaxGateDuration() const { return _maxGateDuration; }

    // Radar Bluetooth (HLK mobile-app pairing) — security-sensitive
    // readRadarBluetoothState(): 1=on, 0=off, -1=error. Determined via
    // readMacAddress NO_MAC sentinel (01:02:03:04:05:08 means BT off).
    int  readRadarBluetoothState();
    // applyBluetoothState(): idempotent — only issues setBluetooth+restart
    // if actual radar state differs from desired. Returns true if final state
    // matches desired (or could not be verified because radar was unreachable).
    bool applyBluetoothState(bool desired);
    // Formatted MAC "AA:BB:CC:DD:EE:FF" into out[18], or sentinel/empty on failure.
    bool getRadarMacString(char out[18]);

    // Engineering Mode
    bool setEngineeringMode(bool enable);
    bool setTrackingMode(bool enable);
    bool isEngineeringMode() const { return _engineeringMode; }
    
    // Gate config verification (ESPHome #13366 workaround)
    bool verifyGateConfig(uint8_t expectedMin, uint8_t expectedMax);

    void setDebug(bool enable) { _debugEnabled = enable; }
    bool isDebugEnabled() const { return _debugEnabled; }

    // Thread-safe copy of gate energies (use this from main loop / MQTT telemetry)
    void getGateEnergiesSafe(uint8_t movOut[14], uint8_t statOut[14]) const;
    uint8_t getLightLevel() const { return getLightLevelSafe(); }  // Always use safe version
    uint8_t getLightLevelSafe() const;

    void getTelemetryJson(JsonDocument& doc);

    // Static zone auto-learning
    bool startStaticLearn(uint16_t duration_s = 180);
    void getLearnResultJson(JsonDocument& doc);
    bool isLearning() const { return _learnActive; }
    bool consumeLearnDone() { bool v = _learnDone; _learnDone = false; return v; }
    int getLearnProgress() const;

private:
    LD2412* _radar = nullptr;
    mutable SemaphoreHandle_t _mutex;
    int8_t _rxPin, _txPin;
    HardwareSerial* _serial = nullptr;
    unsigned long _currentBaud = 115200;  // Track actual running baud rate

    PresenceState _currentState = PresenceState::IDLE;
    MotionType _currentMotionType = MotionType::NONE;
    MotionDirection _currentDirection = MotionDirection::UNKNOWN;

    unsigned long _lastDetectionTime = 0;
    unsigned long _holdTime = 5000;
    std::atomic<bool> _stateChanged{false};

    uint8_t _minMoveEnergy = 0;

    bool _tamperDetected = false;
    bool _tamperExternal = false;
    bool _tamperRadarFailure = false;

    // Health tracking - Enhanced (TASK-005, TASK-006)
    unsigned long _lastValidData = 0;
    bool _initSuccess = false;
    uint8_t _recoveryAttempts = 0;
    unsigned long _lastRecoveryAttempt = 0;
    static constexpr uint8_t MAX_RECOVERY_ATTEMPTS = 5;
    static constexpr unsigned long RECOVERY_INTERVAL = 5000; // 5s between attempts
    static constexpr unsigned long DISCONNECT_TIMEOUT = 2000; // 2s (was 5s) - TASK-006

    void attemptRadarRecovery();

    // Motion data
    uint16_t _lastDist = 0;
    uint8_t _lastMovEn = 0;
    uint8_t _lastStatEn = 0;

    // Debug raw values
    uint16_t _debugMovDist = 0;
    uint16_t _debugStatDist = 0;
    uint8_t _debugMovEn = 0;
    uint8_t _debugStatEn = 0;

    // Gate resolution (cm per gate) — updated by setResolution(), used for gate number in telemetry
    float _gateResolutionCm = 75.0f;

    // Reflector auto-learning (empty room: captures any persistent reflection)
    bool     _learnActive          = false;
    bool     _learnDone            = false; // flag: learn finished, main loop sends notification
    unsigned long _learnStart      = 0;
    uint16_t _learnDuration        = 0;   // seconds
    uint32_t _learnTotalSamples    = 0;
    uint32_t _learnStaticSamples   = 0;   // any detection (moving or static)
    uint32_t _learnGateCount[14]   = {0};
    uint8_t  _learnMaxEnergy[14]   = {0}; // peak energy per gate for sensitivity suggestion
    uint8_t  _learnEnergyType[14]  = {0}; // bitmask per gate: bit0=static, bit1=moving

    // Motion Vector (smoothing)
    static const int DIST_SAMPLES = 5;
    uint16_t _distHistory[DIST_SAMPLES] = {0};
    uint8_t _distIndex = 0;
    uint16_t _trendStartDist = 0;
    unsigned long _lastTrendCalc = 0;

    // Consecutive detection tracking (improved hold time logic)
    uint8_t _consecutiveMisses = 0;
    unsigned long _holdStartTime = 0;

    // Firmware info
    int _fwMajor = 0;
    int _fwMinor = 0;

    // Configuration Cache
    uint8_t _motionSensitivity[14] = {0};
    uint8_t _staticSensitivity[14] = {0};
    uint8_t _minGate = 0;
    uint8_t _maxGate = 0;
    uint8_t _maxGateDuration = 0;

    // Engineering Mode
    bool _engineeringMode = false;
    bool _debugEnabled = false;
    unsigned long _lastEngRecovery = 0;
    uint8_t _engRecoveryAttempts = 0;
    static constexpr uint8_t MAX_ENG_RECOVERY = 3;

    bool _setEngModeInternal(bool enable);

    // Thread-safe copy buffer for getUARTStateString()
    mutable char _uartStateBuf[24] = "INIT";

    // Debug print timing (moved from static - TASK-013)
    unsigned long _lastDebugPrint = 0;
};
#endif
