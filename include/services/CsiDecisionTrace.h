#ifndef CSI_DECISION_TRACE_H
#define CSI_DECISION_TRACE_H

#include <stdint.h>

// P1.2 Decision trace (navrh sekce 17.3): explain the LAST motion decision so
// the frontend / Home Assistant does not have to guess which part of the
// algorithm decided. Pure, header-only, native-testable — the classifier maps
// the already-computed intermediate booleans of _updateMotionState() to a
// single dominant reason; it does NOT re-run the thresholding math.

enum class CsiDecisionReason : uint8_t {
    INSUFFICIENT_SAMPLES = 0,   // turbulence buffer not yet full — no decision made
    VARIANCE_BELOW_THRESHOLD,   // idle: variance under effective threshold, cleanly quiet
    VARIANCE_ABOVE_THRESHOLD,   // motion: variance over effective threshold, smoothing confirmed
    SMOOTHING_ENTER_PENDING,    // raw motion seen but not enough votes to enter → stays idle
    SMOOTHING_EXIT_PENDING,     // raw idle but not enough votes to exit → stays motion (hysteresis)
    BREATHING_HOLD,             // detector idle but breathing/phase hold keeps motion (stationary person)
};

inline const char* csiDecisionReasonStr(CsiDecisionReason r) {
    switch (r) {
        case CsiDecisionReason::INSUFFICIENT_SAMPLES:  return "insufficient_samples";
        case CsiDecisionReason::VARIANCE_BELOW_THRESHOLD: return "variance_below_effective_threshold";
        case CsiDecisionReason::VARIANCE_ABOVE_THRESHOLD: return "variance_above_effective_threshold";
        case CsiDecisionReason::SMOOTHING_ENTER_PENDING: return "smoothing_enter_pending";
        case CsiDecisionReason::SMOOTHING_EXIT_PENDING:  return "smoothing_exit_pending";
        case CsiDecisionReason::BREATHING_HOLD:          return "breathing_hold";
    }
    return "unknown";
}

// Classify the dominant reason for the current motion decision from the
// intermediate values _updateMotionState() has already computed this tick.
//   bufferReady       — turbulence buffer has >= windowSize samples
//   rawMotion         — instantaneous variance-vs-effective-threshold verdict
//   finalMotion       — motion state after this tick (post smoothing/hold)
//   breathHoldApplied — breathing-hold branch kept the previous MOTION state
inline CsiDecisionReason csiClassifyDecision(bool bufferReady, bool rawMotion,
                                             bool finalMotion, bool breathHoldApplied) {
    if (!bufferReady)        return CsiDecisionReason::INSUFFICIENT_SAMPLES;
    if (breathHoldApplied)   return CsiDecisionReason::BREATHING_HOLD;
    if (finalMotion)         return rawMotion ? CsiDecisionReason::VARIANCE_ABOVE_THRESHOLD
                                              : CsiDecisionReason::SMOOTHING_EXIT_PENDING;
    return rawMotion ? CsiDecisionReason::SMOOTHING_ENTER_PENDING
                     : CsiDecisionReason::VARIANCE_BELOW_THRESHOLD;
}

// Snapshot of the last detection decision, exposed read-only via /api/csi/decision.
struct CsiDecisionTrace {
    bool     valid = false;                                  // false until first full-buffer evaluation
    bool     decision = false;                               // motion state after the tick
    CsiDecisionReason reason = CsiDecisionReason::INSUFFICIENT_SAMPLES;
    float    variance = 0.0f;
    float    configuredThreshold = 0.0f;                     // user/base threshold
    float    adaptiveThreshold = 0.0f;                       // adaptive P95 (0 if disabled/unready)
    float    effectiveThreshold = 0.0f;                      // max(configured, adaptive)
    float    hysteresisThreshold = 0.0f;                     // effective * hysteresis (exit level)
    bool     rawMotion = false;
    uint8_t  smoothingVotes = 0;                             // motion bits set in the smoothing window
    uint8_t  smoothingWindow = 0;                            // populated bits in the window
    uint8_t  enterVotes = 0;                                 // votes required to enter MOTION
    uint8_t  exitVotes = 0;                                  // idle votes required to exit MOTION
    bool     breathingHold = false;
    uint16_t breathHoldCount = 0;
    bool     mlMotion = false;
    float    mlProbability = 0.0f;
    bool     radarPresent = false;
    uint32_t activeGeneration = 0;
    uint32_t uptimeMs = 0;
};

#endif // CSI_DECISION_TRACE_H
