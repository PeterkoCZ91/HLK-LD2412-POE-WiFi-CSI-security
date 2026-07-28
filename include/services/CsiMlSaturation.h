#ifndef CSI_ML_SATURATION_H
#define CSI_ML_SATURATION_H

#include <stdint.h>
#include <string.h>

// v5.4.1: ML saturation guard (navrh 2026-07-18, bod 1A). The MLP ships with
// weights and a StandardScaler trained on foreign-site (ESPectre) data; on
// some links the feature vector falls outside the training distribution and
// ml_probability pins near 1.0 regardless of actual motion — measured on both
// test nodes with a HEALTHY packet rate, so csiMlVoteTrusted (pps floor)
// cannot catch it. A vote that says "motion" 24/7 carries no information and
// actively defeats the radar false-positive suppression in fusion.
//
// Pure, native-testable duty-cycle watchdog: per-minute buckets of the final
// ml_motion vote; if the vote was true in >= SATURATE_PCT of the last
// WINDOW_MIN minutes, the vote is declared saturated (untrusted) until it
// behaves for RECOVER_MIN minutes (< RECOVER_PCT duty). Buckets only advance
// on real feeds — a starved link (no CSI frames) does not age the window.
class CsiMlSaturationGuard {
public:
    static constexpr uint16_t WINDOW_MIN   = 360;  // 6 h judgement window
    static constexpr uint16_t RECOVER_MIN  = 60;   // 1 h of good behavior to recover
    static constexpr uint8_t  SATURATE_PCT = 95;
    static constexpr uint8_t  RECOVER_PCT  = 50;

    CsiMlSaturationGuard() { memset(_duty, 0, sizeof(_duty)); }

    // Feed one evaluation of the (smoothed) ml_motion vote.
    void tick(bool mlMotion, uint32_t nowMs) {
        if (_curTicks > 0 && (uint32_t)(nowMs - _curMinuteStart) >= 60000u) {
            _pushBucket((uint8_t)((_curTrue * 100u) / _curTicks));
            _curTicks = 0;
            _curTrue = 0;
            _evaluate();
        }
        if (_curTicks == 0) _curMinuteStart = nowMs;
        _curTicks++;
        if (mlMotion) _curTrue++;
    }

    bool saturated() const { return _saturated; }

    // Average duty (0-100 %) over the newest `minutes` closed buckets;
    // 0 when no closed bucket exists yet.
    uint8_t dutyPct(uint16_t minutes = WINDOW_MIN) const {
        uint16_t n = (_count < minutes) ? _count : minutes;
        if (n == 0) return 0;
        uint32_t sum = 0;
        for (uint16_t i = 0; i < n; i++) {
            // newest-first: last written bucket is at (_head + _count - 1)
            uint16_t idx = (uint16_t)((_head + _count - 1u - i) % WINDOW_MIN);
            sum += _duty[idx];
        }
        return (uint8_t)(sum / n);
    }

    uint16_t bucketCount() const { return _count; }

private:
    void _pushBucket(uint8_t pct) {
        uint16_t idx = (uint16_t)((_head + _count) % WINDOW_MIN);
        if (_count < WINDOW_MIN) {
            _count++;
        } else {
            idx = _head;
            _head = (uint16_t)((_head + 1) % WINDOW_MIN);
        }
        _duty[idx] = pct;
    }

    void _evaluate() {
        if (!_saturated) {
            if (_count >= WINDOW_MIN && dutyPct(WINDOW_MIN) >= SATURATE_PCT)
                _saturated = true;
        } else {
            if (_count >= RECOVER_MIN && dutyPct(RECOVER_MIN) < RECOVER_PCT)
                _saturated = false;
        }
    }

    uint8_t  _duty[WINDOW_MIN];
    uint16_t _head = 0;
    uint16_t _count = 0;
    uint32_t _curMinuteStart = 0;
    uint32_t _curTicks = 0;
    uint32_t _curTrue = 0;
    bool     _saturated = false;
};

#endif
