#ifndef CSI_SERVICE_H
#define CSI_SERVICE_H

#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <Preferences.h>

class MQTTService;

/**
 * WiFi CSI Service — captures Channel State Information alongside Ethernet
 *
 * WiFi runs in STA mode purely as a CSI sensor (packet capture).
 * Network connectivity stays on Ethernet (PoE). WiFi does NOT need
 * internet access — it only needs to associate with an AP for CSI frames.
 *
 * Based on ESPectre by Francesco Pace (GPLv3).
 * Ported to Arduino/PlatformIO standalone for POE-2412 radar+CSI fusion.
 *
 * Features ported from ESPectre:
 *   - HT20/11n WiFi forcing for consistent 64 subcarrier CSI
 *   - STBC and short HT20 packet handling
 *   - Hampel outlier filter (MAD-based)
 *   - Low-pass filter (1st order Butterworth IIR)
 *   - CV normalization (gain-invariant turbulence for ESP32 without AGC lock)
 *   - Breathing-aware presence hold (prevents dropping stationary person)
 *   - DNS traffic generator (UDP queries to gateway for consistent CSI rate)
 *   - Two-pass variance calculation (numerically stable on float32)
 */
class CSIService {
public:
    CSIService();

    void begin(const char* ssid, const char* password,
               MQTTService* mqtt, const char* topicPrefix,
               Preferences* prefs = nullptr);
    void update();

    bool isActive() const { return _active; }

    // Accessors
    float getTurbulence() const { return _lastTurbulence; }
    float getPhaseTurbulence() const { return _lastPhaseTurb; }
    float getRatioTurbulence() const { return _lastRatioTurb; }
    float getBreathingScore() const;
    float getCompositeScore() const;
    float getDser() const { return _lastDser; }
    float getPlcr() const { return _lastPlcr; }
    bool  getMotionState() const { return _motionState; }
    float getVariance() const { return _runningVariance; }
    uint32_t getPacketCount() const { return _totalPackets; }

    // csi10c: HT LTF presence flag — true after first valid HT LTF frame.
    // Stays false when AP emits only HE PHY (WiFi 6 hardware) — lets users
    // distinguish "AP incompatible" from "WiFi not associated" within seconds.
    bool isHtLtfSeen() const { return _htLtfSeen; }

    // Runtime stats
    float getPacketRate() const { return _packetRate; }
    int   getWifiRSSI() const;
    String getWifiSSID() const;
    bool  isIdleInitialized() const { return _idleInitialized; }
    bool  isTrafficGenRunning() const { return _trafficGenRunning.load(); }

    // Configuration getters
    uint16_t getWindowSize()       const { return _windowSize; }
    float    getThreshold()        const { return _threshold; }
    float    getHysteresis()       const { return _hysteresis; }
    uint32_t getPublishInterval()  const { return _publishIntervalMs; }
    uint32_t getTrafficRate()      const { return _trafficRatePps; }
    uint16_t getTrafficPort()      const { return _trafficPort; }
    bool     getTrafficICMP()      const { return _trafficICMP; }

    // Configuration setters
    void setPublishInterval(uint32_t ms) { if (ms < 100) ms = 100; if (ms > 60000) ms = 60000; _publishIntervalMs = ms; }
    void setThreshold(float thr)         { if (thr < 0.001f) thr = 0.001f; if (thr > 100.0f) thr = 100.0f; _threshold = thr; _baseThreshold = thr; _stuckRaiseCount = 0; _stuckMotionCount = 0; }
    void setHysteresis(float hys)        { if (hys < 0.1f) hys = 0.1f; if (hys > 0.99f) hys = 0.99f; _hysteresis = hys; }
    void setWindowSize(uint16_t ws);
    void setTrafficRate(uint32_t pps);
    void setTrafficPort(uint16_t port);
    void setTrafficICMP(bool icmp);

    // Diagnostics actions
    void resetIdleBaseline();
    void forceReconnect();
    void calibrateThreshold(uint32_t durationMs = 10000);
    bool isCalibrating() const { return _calibrating; }
    float getCalibrationProgress() const;
    bool startSiteLearning(uint32_t durationMs);
    void stopSiteLearning();
    void clearLearnedSiteModel();
    bool isSiteLearning() const { return _siteLearningActive; }
    float getSiteLearningProgress() const;
    uint32_t getSiteLearningElapsedSec() const;
    uint32_t getSiteLearningDurationSec() const { return _siteLearnDurationMs / 1000; }
    uint32_t getSiteLearningAcceptedSamples() const { return _siteLearnAccepted; }
    uint32_t getSiteLearningRejectedMotion() const { return _siteLearnRejectedMotion; }
    uint32_t getSiteLearningRejectedRadar()  const { return _siteLearnRejectedRadar; }
    uint32_t getSiteLearningResetBssid()     const { return _siteLearnBssidResetCount; }
    // csi6b: feed radar ground-truth presence into site-learning filter so
    // stationary humans (low CSI variance + breathing hold) don't poison the
    // quiet-site baseline. Called from main loop before csiService.update().
    void notePresenceFromRadar(bool present) { _radarPresent = present; }
    float getSiteLearningThresholdEstimate() const;
    bool hasLearnedSiteModel() const { return _siteModelReady; }
    float getLearnedThreshold() const { return _learnedThreshold; }
    float getLearnedVarianceMean() const { return _learnedMeanVar; }
    float getLearnedVarianceStd() const { return _learnedStdVar; }
    float getLearnedVarianceMax() const { return _learnedMaxVar; }
    uint32_t getLearnedSampleCount() const { return _learnedSampleCount; }
    // csi10: continuous EMA refresh of learned model (adapts to seasonal / furniture changes)
    uint32_t getLearnRefreshCount() const { return _learnRefreshCount; }

    // Auto-calibration on quiet environment
    void setAutoCalibration(bool enabled, uint32_t minutes = 10) {
        _autoCalEnabled = enabled;
        if (minutes < 1) minutes = 1;
        if (minutes > 120) minutes = 120;
        _autoCalQuietSeconds = minutes * 60;
    }
    bool isAutoCalEnabled()   const { return _autoCalEnabled; }
    bool isAutoCalDone()      const { return _autoCalDone; }
    uint32_t getAutoCalQuietSeconds() const { return _autoCalQuietSeconds; }
    uint32_t getAutoCalQuietElapsed() const {
        if (_autoCalQuietStart == 0) return 0;
        return (millis() - _autoCalQuietStart) / 1000UL;
    }
    void resetAutoCalibration() {
        _autoCalDone = false;
        _autoCalQuietStart = 0;
    }

    // csi2: stuck-in-motion auto-raise (port from espectre 02b84e3)
    uint32_t getStuckMotionCount() const { return _stuckMotionCount; }
    uint8_t  getStuckRaiseCount()  const { return _stuckRaiseCount; }
    float    getBaseThreshold()    const { return _baseThreshold; }
    void     resetStuckMotion() { _stuckMotionCount = 0; _stuckRaiseCount = 0; _threshold = _baseThreshold; }

    // csi3: BSSID change detection (auto-reset baseline on AP roam)
    uint32_t getBssidChangeCount() const { return _bssidChangeCount; }

    // csi4: adaptive P95 rolling threshold
    void     setAdaptiveThreshold(bool enabled) { _adaptiveThresholdEnabled = enabled; }
    bool     isAdaptiveThresholdEnabled() const { return _adaptiveThresholdEnabled; }
    float    getAdaptiveThreshold()       const { return _adaptiveThreshold; }
    float    getEffectiveThreshold() const;
    uint16_t getP95SampleCount()    const { return _p95BufCount; }

    // csi5: NBVI subcarrier auto-selection (port from espectre NBVICalibrator, EWMA-lite)
    void     setNbviEnabled(bool enabled) {
        _nbviEnabled = enabled;
        if (!enabled) { _nbviReady = false; _nbviActiveCount = NUM_SUBCARRIERS; }
    }
    bool     isNbviEnabled()     const { return _nbviEnabled; }
    bool     isNbviReady()       const { return _nbviReady; }
    uint16_t getNbviMask()       const;
    uint8_t  getNbviActiveCount()const { return _nbviActiveCount; }
    uint32_t getNbviRecalcCount()const { return _nbviRecalcCount; }
    uint32_t getNbviSamples()    const { return _nbviSamples; }
    uint8_t  getNbviBestSC()     const { return _nbviBestSC; }
    uint8_t  getNbviWorstSC()    const { return _nbviWorstSC; }
    float    getNbviBestScore()  const { return _nbviBestScore; }
    float    getNbviWorstScore() const { return _nbviWorstScore; }

    // csi7b: OTA-aware pause. While set, update() and _restoreEthDefaultNetif()
    // short-circuit so CSI does not touch WiFi/lwIP state during OTA transfer.
    // WiFi reconnects and netif_set_default() otherwise risk dropping the TCP
    // stream mid-upload. Set from ArduinoOTA.onStart / HTTP OTA Update.begin;
    // cleared from onEnd / onError / abort paths.
    static void setOtaInProgress(bool active) { _otaInProgress.store(active); }
    static bool isOtaInProgress() { return _otaInProgress.load(); }

    // csi8: MLP motion detection (15-feature MLP 15->16->8->1, F1=0.795 baseline).
    // Runs in parallel with variance-threshold path so HA can A/B compare ML vs radar.
    void  setMlEnabled(bool enabled) { _mlEnabled = enabled; if (!enabled) _mlMotion = false; }
    bool  isMlEnabled()       const { return _mlEnabled; }
    float getMlProbability()  const { return _mlProbability; }
    bool  getMlMotionState()  const { return _mlMotion; }
    float getMlThreshold()    const { return _mlThreshold; }
    void  setMlThreshold(float thr) {
        if (thr < 0.05f) thr = 0.05f;
        if (thr > 0.95f) thr = 0.95f;
        _mlThreshold = thr;
    }

private:
    static void _csiCallback(void* ctx, wifi_csi_info_t* info);
    void _processCSI(wifi_csi_info_t* info);
    void _publishMQTT();
    void _updateMotionState();
    void _initWiFiForCSI(const char* ssid, const char* password);
    void _restoreEthDefaultNetif();
    void _startTrafficGen();
    void _stopTrafficGen();
    static void _trafficGenTask(void* arg);
    void _loadLearnedModel();
    void _saveLearnedModel(float threshold, float meanVar, float stdVar, float maxVar, uint32_t samples);
    float _computeSiteLearningThreshold() const;
    void _finalizeSiteLearning();
    void _runMlInference();
    void _continuousLearnRefresh();

    // Configuration
    uint16_t _windowSize = 75;
    float _threshold = 0.5f;
    float _hysteresis = 0.7f;
    uint32_t _publishIntervalMs = 1000;
    uint32_t _trafficRatePps = 100;
    uint16_t _trafficPort = 7;      // Default: echo port (7), alt: 53 (DNS)
    bool     _trafficICMP = false;   // ICMP echo (ping) mode — better response rate

    // HT20 subcarrier selection (12 subcarriers, avoiding guard bands <11 and >52, DC=32)
    static constexpr uint8_t NUM_SUBCARRIERS = 12;
    static constexpr uint8_t SUBCARRIERS[12] = {12, 14, 16, 18, 20, 24, 28, 36, 40, 44, 48, 52};

    // HT20 constants
    static constexpr uint16_t HT20_CSI_LEN = 128;        // 64 SC × 2 bytes
    static constexpr uint16_t HT20_CSI_LEN_DOUBLE = 256;  // STBC doubled
    static constexpr uint16_t HT20_CSI_LEN_SHORT = 114;   // 57 SC × 2 bytes
    static constexpr uint8_t  HT20_SHORT_LEFT_PAD = 8;    // 4 SC × 2 bytes guard padding

    // Circular turbulence buffer
    float* _turbBuffer = nullptr;
    uint16_t _bufIndex = 0;
    uint16_t _bufCount = 0;

    // Running variance (two-pass on publish, Welford's incremental for interim)
    float _runningMean = 0.0f;
    float _runningM2 = 0.0f;
    float _runningVariance = 0.0f;

    // Per-packet signals
    float _lastTurbulence = 0.0f;
    float _lastPhaseTurb = 0.0f;
    float _lastRatioTurb = 0.0f;
    float _lastAmpSum = 0.0f;  // for idle baseline tracking

    // DSER/PLCR (Uni-Fi features, arXiv 2601.10980)
    float _lastDser = 0.0f;
    float _lastPlcr = 0.0f;
    float _csiStatic[NUM_SUBCARRIERS] = {0};       // per-SC slow EMA amplitude (H_s)
    float _csiPhasePrev[NUM_SUBCARRIERS] = {0};    // per-SC previous phase (t-1)
    bool  _hasPrevPhase = false;
    static constexpr float DSER_STATIC_ALPHA = 0.01f;  // ~100-packet EMA time constant
    static constexpr float DSER_EPS = 1e-3f;

    // Hampel filter state (MAD-based outlier removal)
    static constexpr uint8_t HAMPEL_WINDOW = 7;
    static constexpr float HAMPEL_THRESHOLD = 5.0f;
    static constexpr float MAD_SCALE = 1.4826f;
    struct {
        float buffer[11];  // max window size
        uint8_t index = 0;
        uint8_t count = 0;
    } _hampelState;

    // Low-pass filter state (1st order Butterworth IIR)
    struct {
        float b0 = 0;
        float a1 = 0;
        float x_prev = 0;
        float y_prev = 0;
        bool initialized = false;
    } _lowpassState;

    // Breathing bandpass filter state
    struct {
        float hp_x_prev = 0, hp_y_prev = 0;
        float lp_x_prev = 0, lp_y_prev = 0;
        float energy = 0;
        bool initialized = false;
    } _breathFilter;

    // Temporal smoothing
    static constexpr uint8_t SMOOTH_WINDOW = 6;
    static constexpr uint8_t SMOOTH_ENTER = 4;  // espectre: 4/6 to enter MOTION
    static constexpr uint8_t SMOOTH_EXIT = 5;   // espectre: 5/6 to exit
    uint8_t _smoothHistory = 0;
    uint8_t _smoothCount = 0;

    // Motion state
    bool _motionState = false;

    // Breathing-aware presence hold
    uint16_t _breathHoldCount = 0;
    static constexpr uint16_t BREATH_HOLD_MAX = 300; // ~5 min at 1s publish

    // Idle baselines (EMA)
    float _idleMeanTurb = 0;
    float _idleMeanPhase = 0;
    float _idleAmpBaseline = 0;
    bool _idleInitialized = false;

    // Timing
    uint32_t _totalPackets = 0;
    uint32_t _windowPackets = 0;
    volatile bool _htLtfSeen = false;  // csi10c: set in _processCSI on first valid HT LTF frame
    uint32_t _lastPublishMs = 0;
    float    _packetRate = 0.0f;
    bool     _reconnectRequested = false;

    // Calibration
    bool     _calibrating = false;
    uint32_t _calibStartMs = 0;
    uint32_t _calibDurationMs = 0;
    float    _calibVarSum = 0.0f;
    uint32_t _calibSamples = 0;

    // Auto-calibration on quiet environment (port from espectre 503ec04)
    bool     _autoCalEnabled = true;
    uint32_t _autoCalQuietSeconds = 600;
    bool     _autoCalDone = false;
    uint32_t _autoCalQuietStart = 0;

    // csi2: stuck-in-motion auto-raise
    uint32_t _stuckMotionCount = 0;
    uint8_t  _stuckRaiseCount = 0;
    float    _baseThreshold = 0.5f;
    static constexpr uint32_t STUCK_MOTION_LIMIT = 86400;
    static constexpr uint8_t  STUCK_RAISE_MAX = 3;
    static constexpr float    STUCK_RAISE_FACTOR = 1.5f;

    // csi3: BSSID change detection
    uint8_t  _lastBSSID[6] = {0};
    bool     _bssidInitialized = false;
    uint32_t _bssidChangeCount = 0;

    // csi4: adaptive P95 rolling threshold
    static constexpr uint16_t P95_BUFFER_SIZE = 300;
    static constexpr uint16_t P95_UPDATE_EVERY = 30;
    static constexpr float    P95_FACTOR = 1.1f;
    float    _p95Buffer[P95_BUFFER_SIZE] = {0};
    uint16_t _p95BufIndex = 0;
    uint16_t _p95BufCount = 0;
    uint16_t _p95TickSinceUpdate = 0;
    bool     _adaptiveThresholdEnabled = true;
    float    _adaptiveThreshold = 0.0f;

    // csi5: NBVI-lite subcarrier auto-selection
    static constexpr float    NBVI_ALPHA        = 0.01f;
    static constexpr uint32_t NBVI_MIN_SAMPLES  = 200;
    static constexpr uint32_t NBVI_RECALC_EVERY = 500;
    static constexpr uint8_t  NBVI_SELECT_K     = 8;
    static constexpr float    NBVI_EPS          = 1e-6f;
    bool     _nbviEnabled = true;
    bool     _nbviReady = false;
    float    _nbviMean[12]   = {0};
    float    _nbviVar[12]    = {0};
    float    _nbviScore[12]  = {0};
    uint8_t  _nbviMask[12]   = {1,1,1,1,1,1,1,1,1,1,1,1};
    uint8_t  _nbviActiveCount = 12;
    uint32_t _nbviSamples     = 0;
    uint32_t _nbviLastRecalcSamples = 0;
    uint32_t _nbviRecalcCount = 0;
    uint8_t  _nbviBestSC      = 0;
    uint8_t  _nbviWorstSC     = 0;
    float    _nbviBestScore   = 0;
    float    _nbviWorstScore  = 0;

    // csi6: long-term quiet-site learning (hours/days). Saved to NVS via _prefs.
    Preferences* _prefs = nullptr;
    bool     _siteLearningActive = false;
    uint32_t _siteLearnStartMs = 0;
    uint32_t _siteLearnDurationMs = 0;
    uint32_t _siteLearnAccepted = 0;
    uint32_t _siteLearnRejectedMotion = 0;
    uint32_t _siteLearnRejectedRadar  = 0;
    uint32_t _siteLearnBssidResetCount = 0;
    bool     _radarPresent = false;
    float    _siteLearnMeanVarAcc = 0.0f;
    float    _siteLearnM2Var = 0.0f;
    float    _siteLearnMaxVar = 0.0f;
    bool     _siteModelReady = false;
    float    _learnedThreshold = 0.0f;
    // csi10e: absolute floor for learned variance threshold. Below this the
    // baseline becomes a hair-trigger — even normal WiFi jitter, weather, or
    // distant-room RF drift will exceed it and false-positive the CSI signal.
    // Observed production false-alarm on 2026-04-24 had learned_threshold=0.001.
    static constexpr float MIN_LEARNED_THRESHOLD = 0.005f;
    float    _learnedMeanVar = 0.0f;
    float    _learnedStdVar = 0.0f;
    float    _learnedMaxVar = 0.0f;
    uint32_t _learnedSampleCount = 0;
    float    _learnedIdleMeanTurb = 0.0f;
    float    _learnedIdleMeanPhase = 0.0f;
    float    _learnedIdleAmpBaseline = 0.0f;
    // csi10: continuous EMA refresh state
    unsigned long _lastLearnRefreshSaveMs = 0;
    uint32_t _learnRefreshCount = 0;

    // csi8: MLP motion detection state (15 -> 16 -> 8 -> 1, dual-threshold hysteresis)
    bool     _mlEnabled     = true;
    float    _mlThreshold   = 0.50f;      // enter threshold; exit = threshold * ML_EXIT_FACTOR
    float    _mlProbability = 0.0f;
    bool     _mlMotion      = false;
    static constexpr float ML_EXIT_FACTOR = 0.70f;  // exit = 0.35 when threshold = 0.50
    // Independent N/M smoothing over ML raw decisions (shape-aligned with variance path)
    uint8_t  _mlSmoothHistory = 0;
    uint8_t  _mlSmoothCount   = 0;

    // Traffic generator
    TaskHandle_t _trafficGenHandle = nullptr;
    int _trafficGenSock = -1;
    std::atomic<bool> _trafficGenRunning{false};
    static std::atomic<bool> _otaInProgress;

    // State
    bool _active = false;
    MQTTService* _mqtt = nullptr;
    char _topicPrefix[64] = {};

    // Publish topics
    char _tMotion[80] = {};
    char _tTurbulence[80] = {};
    char _tVariance[80] = {};
    char _tPhaseTurb[80] = {};
    char _tRatioTurb[80] = {};
    char _tBreathing[80] = {};
    char _tComposite[80] = {};
    char _tPackets[80] = {};
    char _tDser[80] = {};
    char _tPlcr[80] = {};
    char _tMlProb[80] = {};
    char _tMlMotion[80] = {};
};

#endif // CSI_SERVICE_H
