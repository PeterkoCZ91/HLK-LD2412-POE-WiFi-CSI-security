#ifndef CSI_SHADOW_DETECTOR_H
#define CSI_SHADOW_DETECTOR_H

#include <stdint.h>

// P1.1 Shadow evaluation (navrh sekce 17.1): run the candidate model's motion
// verdict in parallel with the active model, WITHOUT any effect on production
// motion, alarm or PDU. This is a self-contained mirror of the variance path in
// CSIService::_updateMotionState() (N/M temporal smoothing + hysteresis) driven
// by the SAME per-tick variance but the CANDIDATE threshold. It keeps its own
// smoothing history, so feeding it can never mutate active detection state.
//
// Breathing-hold is intentionally NOT mirrored here: it depends on the active
// model's idle baselines and is a small refinement on top of the variance path.
// The shadow is a faithful approximation of the candidate's variance verdict,
// which is what "would this threshold have fired?" needs to answer.

struct CsiShadowConfig {
    uint8_t window = 6;   // SMOOTH_WINDOW
    uint8_t enter  = 4;   // SMOOTH_ENTER (votes to enter MOTION)
    uint8_t exit   = 5;   // SMOOTH_EXIT  (idle votes to exit MOTION)
};

class CsiShadowDetector {
public:
    explicit CsiShadowDetector(CsiShadowConfig cfg = {}) : _cfg(cfg) {}

    void reset() { _hist = 0; _count = 0; _motion = false; }
    bool motion() const { return _motion; }

    // Feed one variance sample against the candidate threshold; returns the
    // updated shadow motion state. `hysteresis` (<1) sets the exit level, matching
    // the active detector's rawMotion computation.
    bool update(float variance, float threshold, float hysteresis) {
        bool raw = _motion ? (variance >= threshold * hysteresis)
                           : (variance > threshold);

        _hist = (uint8_t)(((_hist << 1) | (raw ? 1 : 0)) & ((1u << _cfg.window) - 1));
        if (_count < _cfg.window) _count++;

        uint8_t votes = 0, h = _hist;
        for (uint8_t i = 0; i < _count; i++) { votes += (h & 1); h >>= 1; }

        if (!_motion) {
            _motion = (votes >= _cfg.enter && _count >= _cfg.enter);
        } else {
            uint8_t idle = (uint8_t)(_count - votes);
            _motion = !(idle >= _cfg.exit && _count >= _cfg.exit);
        }
        return _motion;
    }

private:
    CsiShadowConfig _cfg;
    uint8_t _hist = 0;
    uint8_t _count = 0;
    bool    _motion = false;
};

#endif // CSI_SHADOW_DETECTOR_H
