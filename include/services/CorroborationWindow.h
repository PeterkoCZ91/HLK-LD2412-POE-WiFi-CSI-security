#ifndef CORROBORATION_WINDOW_H
#define CORROBORATION_WINDOW_H

// #3 Corroboration window. Low-confidence detections wait briefly for a
// second independent modality. It sits before AlarmFSM::reportMotion so the
// alarm state machine remains unchanged. Pure/Arduino-free for native tests.
#include <cstdint>

enum class CorrGate : uint8_t {
    PASS,
    HOLD,
    SUPPRESS,
};

struct CorrInputs {
    bool qualifies = false;
    float confidence = 0.0f;
    uint8_t fusionSource = 0;
    uint32_t nowMs = 0;
};

class CorroborationWindow {
public:
    float confThreshold = 0.6f;
    uint32_t windowMs = 8000;

    void reset() {
        _open = false;
        _openMs = 0;
        _openSource = 0;
    }

    CorrGate evaluate(const CorrInputs& in) {
        if (!in.qualifies) {
            reset();
            return CorrGate::PASS;
        }
        if (in.confidence >= confThreshold) {
            reset();
            return CorrGate::PASS;
        }

        if (!_open) {
            _open = true;
            _openMs = in.nowMs;
            _openSource = in.fusionSource;
            return CorrGate::HOLD;
        }

        if ((in.fusionSource & ~_openSource) != 0) {
            reset();
            return CorrGate::PASS;
        }

        if ((uint32_t)(in.nowMs - _openMs) >= windowMs) {
            reset();
            return CorrGate::SUPPRESS;
        }
        return CorrGate::HOLD;
    }

    bool windowOpen() const { return _open; }

private:
    bool _open = false;
    uint32_t _openMs = 0;
    uint8_t _openSource = 0;
};

#endif  // CORROBORATION_WINDOW_H
