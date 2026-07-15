#ifndef CSI_SERVICE_H
#define CSI_SERVICE_H

#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <Preferences.h>
#include "services/CsiSiteModel.h"
#include "services/NvsCsiModelStore.h"
#include "services/CsiModelManager.h"
#include "services/CsiDecisionTrace.h"
#include "services/CsiEventRing.h"
#include "services/CsiShadowDetector.h"
#include "services/CsiHealthReasons.h"

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

    // P1.2 decision trace — read-only snapshot of the last motion decision
    // (navrh 17.3). Exposed via GET /api/csi/decision; purely diagnostic.
    const CsiDecisionTrace& getDecisionTrace() const { return _decisionTrace; }

    // P1.3 diagnostic event ring (navrh 17.2) — read-only access for
    // GET/DELETE /api/csi/events. RAM only; edges/spikes/disagreements/health.
    uint16_t copyEventsAfter(uint32_t afterSeq, uint16_t limit, CsiEvent* out, uint16_t outCap) const {
        return _events.query(afterSeq, limit, out, outCap);
    }
    void     clearEvents()          { _events.clear(); }
    uint32_t lastEventSeq()   const { return _events.lastSeq(); }
    uint16_t eventCount()     const { return _events.count(); }
    uint16_t eventCapacity()  const { return _events.capacity(); }

    // P1.1 shadow evaluation (navrh 17.1) — candidate verdict computed in
    // parallel, DIAGNOSTIC ONLY, never affects motion/alarm/PDU. Active only
    // when a candidate model exists.
    bool     isShadowActive()      const { return hasCandidateModel(); }
    bool     getShadowMotion()     const { return _shadowMotion; }
    uint32_t getShadowAgree()      const { return _shadowAgree; }
    uint32_t getShadowDisagree()   const { return _shadowDisagree; }
    float    getShadowThreshold()  const { return hasCandidateModel() ? modelCandidate().threshold : 0.0f; }

    // csi10c: HT LTF presence flag — true after first valid HT LTF frame.
    // Stays false when AP emits only HE PHY (WiFi 6 hardware) — lets users
    // distinguish "AP incompatible" from "WiFi not associated" within seconds.
    bool isHtLtfSeen() const { return _htLtfSeen; }

    // Runtime stats
    float getPacketRate() const { return _packetRate; }
    // P1.4 health: capture rate swinging far from its own running average
    bool  isPacketRateUnstable() const {
        return _packetRateEma > 1.0f &&
               fabsf(_packetRate - _packetRateEma) > 0.5f * _packetRateEma;
    }
    // P1.4 health: true if the AP/BSSID changed within the last `windowMs` ms
    bool  hasRoamedWithin(uint32_t windowMs) const {
        return _bssidChangeCount > 0 && _lastBssidChangeMs != 0 &&
               (millis() - _lastBssidChangeMs) < windowMs;
    }
    // P1.4 health: fraction of site-learning samples rejected (0 if not learning / no data)
    float learningRejectRatio() const {
        uint32_t total = _siteLearnAccepted + _siteLearnRejectedMotion + _siteLearnRejectedRadar;
        if (total == 0) return 0.0f;
        return (float)(_siteLearnRejectedMotion + _siteLearnRejectedRadar) / (float)total;
    }
    int   getWifiRSSI() const;
    uint8_t getLastDisconnectReason() const { return _lastDisconnectReason; }
    uint32_t getReconnectAttempts() const { return _reconnectAttempts; }
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
    // OTA single-homing: drop / restore the CSI WiFi STA so the device isn't dual-homed
    // on the Ethernet subnet during a flash (dual-homing intermittently breaks espota).
    // update()'s reconnect loop is already gated by _otaInProgress, so WiFi stays down
    // between these calls. See docs/OTA_OPERATIONS.md (dual-homing).
    void wifiDownForOta();
    void wifiUpAfterOta();
    void calibrateThreshold(uint32_t durationMs = 10000);
    bool isCalibrating() const { return _calibrating; }
    float getCalibrationProgress() const;
    bool startSiteLearning(uint32_t durationMs, bool replaceCandidate = false);
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

    // csi-model: active/candidate/previous slot management (site-learning v2).
    // finalize now produces a CANDIDATE only; apply/rollback switch detection.
    CsiModelOp applyCandidateModel();
    CsiModelOp rollbackSiteModel();
    CsiModelOp clearCandidateModel();
    // P1.5 import (navrh 17.5): land an externally-supplied model as CANDIDATE
    // only — never active. Assigns a fresh generation, validates+seals via the
    // model manager, and requires an explicit apply afterwards.
    CsiModelOp importCandidateModel(const CsiSiteModel& src);
    bool hasCandidateModel() const { return _modelMgr.hasCandidate(); }
    bool hasPreviousModel()  const { return _modelMgr.hasPrevious(); }
    const CsiSiteModel& modelActive()    const { return _modelMgr.active(); }
    const CsiSiteModel& modelCandidate() const { return _modelMgr.candidate(); }
    const CsiSiteModel& modelPrevious()  const { return _modelMgr.previous(); }
    uint32_t activeModelGeneration() const { return _modelMgr.active().generation; }
    bool getModelQuality(CsiModelQuality& out) const {
        if (!_hasQuality) return false; out = _lastQuality; return true;
    }

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
    void _recordDecisionTrace(bool bufferReady, bool rawMotion, bool finalMotion,
                              bool breathHold, uint8_t votes, uint8_t window, float effThr);
    void _pushEvent(CsiEventType type, float effThr, float shadowThr = 0.0f,
                    bool shadowMotion = false, uint16_t healthFlags = 0);
    float _relativeFloor() const;  // v5.4: 3x learned quiet mean (link-relative)
    void _updateShadow(float effThr);  // P1.1: run candidate verdict in parallel
    uint16_t _computeCsiHealthFlags() const;  // P1.4: CSI-intrinsic health subset
    void _updateHealthEvents(float effThr);    // emit HEALTH_CHANGE on transition
    void _initWiFiForCSI(const char* ssid, const char* password);
    void _restoreEthDefaultNetif();
    void _startTrafficGen();
    void _stopTrafficGen();
    static void _trafficGenTask(void* arg);
    void _loadLearnedModel();
    void _saveLearnedModel(float threshold, float meanVar, float stdVar, float maxVar, uint32_t samples);
    float _computeSiteLearningThreshold() const;
    void _finalizeSiteLearning();
    void _publishModelState(const char* event);  // retained active/candidate JSON + event, on model change only
    void _applyActiveToRuntime();     // mirror active slot into _learned*/idle baseline (no _threshold change)
    void _switchDetectionToActive();  // apply/rollback only: move detection _threshold to active model
    void _resetShortTermState();      // flush P95/hysteresis/running stats + reinit EMA timer
    void _runMlInference();
    void _continuousLearnRefresh();

    // Configuration
    uint16_t _windowSize = 75;
    float _threshold = 0.5f;
    float _hysteresis = 0.5f;   // v5.4: 0.7 -> 0.5 (value deployed on the working ESPectre fleet)
    uint32_t _publishIntervalMs = 1000;
    uint32_t _trafficRatePps = 100;
    uint16_t _trafficPort = 7;      // Only used in UDP mode; irrelevant when ICMP default is active
    // v5.4: default ICMP, not UDP:7 — confirmed in the field that some ISP-
    // provided 5G/CPE routers throttle/filter unsolicited UDP:7 as a DDoS
    // reflection heuristic (port 7 = echo, classic amplification target),
    // dropping capture rate to ~1% of target even at good RSSI. ICMP echo
    // is universally allowed.
    bool     _trafficICMP = true;

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

    // P1.2 decision trace — filled at every _updateMotionState() call
    CsiDecisionTrace _decisionTrace;

    // P1.3 diagnostic event ring (RAM only). 256 events ≈ 11 KB.
    static constexpr uint16_t CSI_EVENT_RING_CAP = 256;
    CsiEventRing<CSI_EVENT_RING_CAP> _events;
    uint32_t _lastSpikeEventMs = 0;               // rate-limit VARIANCE_SPIKE
    static constexpr uint32_t SPIKE_EVENT_MIN_GAP_MS = 10000;
    static constexpr float    SPIKE_VARIANCE_FACTOR  = 3.0f;

    // P1.1 shadow evaluation — candidate verdict in parallel, diagnostic only.
    CsiShadowDetector _shadow;
    bool     _shadowMotion   = false;
    uint32_t _shadowAgree    = 0;
    uint32_t _shadowDisagree = 0;
    uint32_t _shadowCandGen  = 0;    // candidate generation the counters belong to
    bool     _pubShadowMotion = false;

    // P1.4 health-change events — CSI-intrinsic flag subset tracked per tick.
    // v5.3.1: debounced (flag set must hold ~10 ticks) so a boundary-oscillating
    // flag can't fill the ring; evaluated EVERY tick incl. starved (empty window).
    CsiHealthDebounce _healthDebounce{10};

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
    volatile uint8_t _lastDisconnectReason = 0;  // DIAG: last WiFi STA disconnect reason (remote diagnostics)
    volatile uint32_t _reconnectAttempts = 0;    // DIAG: count of background WiFi reconnect attempts (out-of-coverage visibility)
    uint32_t _lastPublishMs = 0;
    float    _packetRate = 0.0f;
    float    _packetRateEma = 0.0f;   // P1.4: slow average of _packetRate for stability check
    bool     _reconnectRequested = false;

    // MQTT change-gating: last published values + heartbeat clock. Floats go
    // out when they move, states on flip; heartbeat republishes everything.
    uint32_t _mqttHeartbeatMs = 0;
    uint32_t _floatPaceMs = 0;    // floats evaluated at most once per 10 s (noisy)
    bool     _pubValid = false;   // false until first full publish after (re)connect
    bool     _pubMotion = false, _pubMlMotion = false;
    float    _pubTurbulence = 0, _pubVariance = 0, _pubPhaseTurb = 0, _pubRatioTurb = 0;
    float    _pubBreathing = 0, _pubComposite = 0, _pubDser = 0, _pubPlcr = 0, _pubMlProb = 0;

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
    uint32_t _lastBssidChangeMs = 0;   // P1.4: millis() of last roam (0 = never)

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
    // v5.4: the old MIN_LEARNED_THRESHOLD=0.005 absolute floor is gone — it sat
    // above walking peaks on strong (CV-compressed) links and blinded detection
    // by design. Floors are link-relative now: csiModelRelativeFloor() in
    // CsiSiteModel.h (3x quiet mean, absolute sanity bound 1e-4). The 2026-04-24
    // hair-trigger case is still covered: a noisy site has a high quiet mean,
    // so its relative floor lands at/above the old 0.005 automatically.
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

    // csi-model: active/candidate/previous slot manager + quality report.
    // _modelStore MUST be declared before _modelMgr (ctor takes it by reference).
    NvsCsiModelStore     _modelStore;
    CsiModelManager      _modelMgr{_modelStore};
    CsiModelQuality      _lastQuality;
    bool                 _hasQuality = false;
    CsiVarianceHistogram _varHist;   // variance quantiles accumulated during learning

    // csi8: MLP motion detection state (15 -> 16 -> 8 -> 1, dual-threshold hysteresis)
    bool     _mlEnabled     = true;
    float    _mlThreshold   = 0.50f;      // enter threshold; exit = threshold * ML_EXIT_FACTOR
    float    _mlProbability = 0.0f;
    bool     _mlMotion      = false;
    static constexpr float ML_EXIT_FACTOR = 0.70f;  // exit = 0.35 when threshold = 0.50
    // Same usable-capture floor as CSI_HEALTH_PACKET_RATE_LOW (CsiHealthReasons.h) —
    // below it, DSER/turbulence time constants are stretched far past their trained
    // real-time window and ml_probability saturates regardless of actual motion.
    static constexpr float ML_MIN_PACKET_RATE_PPS = 5.0f;
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
    char _tShadow[80] = {};   // P1.1: diagnostic candidate-shadow JSON (retained)
};

#endif // CSI_SERVICE_H
