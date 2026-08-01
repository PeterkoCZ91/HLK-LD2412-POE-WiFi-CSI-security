#ifndef CROSS_MODAL_LIVENESS_H
#define CROSS_MODAL_LIVENESS_H

// #1 Cross-modal liveness. Health flags detect "no packets"; this detects the
// subtler failure — a sensor that runs and returns data but stops correlating
// with the other one. Over a rolling window we count how long each modality was
// active and how often the other modality corroborated it within a short lag.
// Pure/Arduino-free so it host-tests like CsiTamperDetector.
#include <cstdint>

enum CrossModal : uint32_t {
    CROSSMODAL_NONE = 0,
    CROSSMODAL_RADAR_UNCORROBORATED = 1u << 0,
    CROSSMODAL_CSI_UNCORROBORATED = 1u << 1,
};

struct CrossModalInputs {
    bool radarSees = false;
    bool csiSees = false;
    bool bothLive = false;
    uint32_t nowMs = 0;
};

class CrossModalLiveness {
public:
    uint32_t evalWindowMs = 600000;
    uint32_t corrLagMs = 8000;
    uint32_t minActiveMs = 60000;
    float minCorrRatio = 0.2f;

    void reset() {
        _windowStart = 0;
        _seen = false;
        _radarActiveMs = _radarCorrMs = 0;
        _csiActiveMs = _csiCorrMs = 0;
        _lastMs = 0;
        _radarLastSeenMs = _csiLastSeenMs = 0;
        _flags = CROSSMODAL_NONE;
    }

    // Call every frame. Returns the latched flag set, updated at each window end.
    uint32_t update(const CrossModalInputs& in) {
        if (!in.bothLive) {
            reset();
            return CROSSMODAL_NONE;
        }
        if (!_seen) {
            _seen = true;
            _windowStart = in.nowMs;
            _lastMs = in.nowMs;
        }

        uint32_t dt = (uint32_t)(in.nowMs - _lastMs);
        _lastMs = in.nowMs;
        if (in.radarSees) {
            _radarLastSeenMs = in.nowMs;
        }
        if (in.csiSees) {
            _csiLastSeenMs = in.nowMs;
        }

        if (in.radarSees) {
            _radarActiveMs += dt;
            if (in.csiSees || (uint32_t)(in.nowMs - _csiLastSeenMs) <= corrLagMs) {
                _radarCorrMs += dt;
            }
        }
        if (in.csiSees) {
            _csiActiveMs += dt;
            if (in.radarSees || (uint32_t)(in.nowMs - _radarLastSeenMs) <= corrLagMs) {
                _csiCorrMs += dt;
            }
        }

        if ((uint32_t)(in.nowMs - _windowStart) >= evalWindowMs) {
            uint32_t f = CROSSMODAL_NONE;
            if (_radarActiveMs >= minActiveMs &&
                ratio(_radarCorrMs, _radarActiveMs) < minCorrRatio) {
                f |= CROSSMODAL_RADAR_UNCORROBORATED;
            }
            if (_csiActiveMs >= minActiveMs &&
                ratio(_csiCorrMs, _csiActiveMs) < minCorrRatio) {
                f |= CROSSMODAL_CSI_UNCORROBORATED;
            }
            _flags = f;
            _windowStart = in.nowMs;
            _radarActiveMs = _radarCorrMs = _csiActiveMs = _csiCorrMs = 0;
        }
        return _flags;
    }

    uint32_t flags() const { return _flags; }

private:
    static float ratio(uint32_t part, uint32_t whole) {
        return whole == 0 ? 1.0f : (float)part / (float)whole;
    }

    bool _seen = false;
    uint32_t _windowStart = 0;
    uint32_t _lastMs = 0;
    uint32_t _radarActiveMs = 0;
    uint32_t _radarCorrMs = 0;
    uint32_t _csiActiveMs = 0;
    uint32_t _csiCorrMs = 0;
    uint32_t _radarLastSeenMs = 0;
    uint32_t _csiLastSeenMs = 0;
    uint32_t _flags = CROSSMODAL_NONE;
};

inline int renderCrossModal(uint32_t flags, char* buf, unsigned bufLen) {
    struct Reason {
        uint32_t bit;
        const char* text;
    };
    const Reason reasons[] = {
        {CROSSMODAL_RADAR_UNCORROBORATED,
         "radar motion never corroborated by CSI (CSI blind?)"},
        {CROSSMODAL_CSI_UNCORROBORATED,
         "CSI motion never corroborated by radar (radar blind?)"},
    };
    int count = 0;
    if (bufLen) {
        buf[0] = '\0';
    }
    for (const auto& reason : reasons) {
        if (!(flags & reason.bit)) {
            continue;
        }
        unsigned used = 0;
        while (used < bufLen && buf[used]) {
            used++;
        }
        if (count > 0 && used + 2 < bufLen) {
            buf[used++] = ';';
            buf[used++] = ' ';
            buf[used] = '\0';
        }
        for (const char* p = reason.text; *p && used + 1 < bufLen; ++p) {
            buf[used++] = *p;
            buf[used] = '\0';
        }
        count++;
    }
    return count;
}

#endif  // CROSS_MODAL_LIVENESS_H
