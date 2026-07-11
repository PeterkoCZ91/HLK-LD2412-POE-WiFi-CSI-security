#ifndef CSI_TAMPER_DETECTOR_H
#define CSI_TAMPER_DETECTOR_H

// #5 CSI-side tamper detection. Anti-masking today only watches the radar; the
// passive CSI sensor can be blinded by pulling/covering the AP without the radar
// ever noticing. This pure, stateful detector flags two blind-states:
//   - NO_PACKETS: packet rate collapses to ~0 while Ethernet is still up (so it
//     is not just a network outage) for longer than a grace window.
//   - FROZEN:    running variance is bit-identical for a long window — a stuck
//     DMA / frozen capture returning the same frame forever.
// Arduino-free so it can be host-tested; SecurityMonitor feeds it live inputs.

#include <cstdint>

enum CsiTamper : uint32_t {
    CSI_TAMPER_NONE       = 0,
    CSI_TAMPER_NO_PACKETS = 1u << 0,
    CSI_TAMPER_FROZEN     = 1u << 1,
};

struct CsiTamperInputs {
    bool     csiActive = false;    // CSI meant to be sensing right now
    bool     ethUp = false;        // Ethernet link up (rules out network outage)
    float    packetRate = 0.0f;    // pps
    float    variance = 0.0f;      // running variance
    uint32_t nowMs = 0;
};

class CsiTamperDetector {
public:
    // Config (ms / pps). Generous defaults — tamper is a sustained state, not a blip.
    uint32_t noPacketGraceMs = 30000;
    uint32_t frozenGraceMs   = 120000;
    float    minPps          = 1.0f;

    void reset() {
        _lowPpsSinceMs = 0;
        _lastVariance = -1.0f;
        _varChangedMs = 0;
        _seen = false;
    }

    // Returns the current tamper bitmask (0 = healthy). Call periodically.
    uint32_t update(const CsiTamperInputs& in) {
        if (!in.csiActive) { reset(); return CSI_TAMPER_NONE; }

        uint32_t flags = CSI_TAMPER_NONE;

        // --- packet collapse ---
        if (in.ethUp && in.packetRate < minPps) {
            if (_lowPpsSinceMs == 0) _lowPpsSinceMs = in.nowMs ? in.nowMs : 1;
            else if ((uint32_t)(in.nowMs - _lowPpsSinceMs) >= noPacketGraceMs)
                flags |= CSI_TAMPER_NO_PACKETS;
        } else {
            _lowPpsSinceMs = 0;
        }

        // --- frozen variance ---
        if (!_seen || in.variance != _lastVariance) {
            _lastVariance = in.variance;
            _varChangedMs = in.nowMs;
            _seen = true;
        } else if ((uint32_t)(in.nowMs - _varChangedMs) >= frozenGraceMs) {
            flags |= CSI_TAMPER_FROZEN;
        }

        return flags;
    }

private:
    uint32_t _lowPpsSinceMs = 0;   // 0 = pps currently ok
    float    _lastVariance = -1.0f;
    uint32_t _varChangedMs = 0;
    bool     _seen = false;
};

inline int renderCsiTamper(uint32_t flags, char* buf, unsigned bufLen) {
    struct { uint32_t bit; const char* txt; } M[] = {
        { CSI_TAMPER_NO_PACKETS, "CSI packets stopped (AP pulled/covered?)" },
        { CSI_TAMPER_FROZEN,     "CSI variance frozen (capture stuck)" },
    };
    int n = 0;
    if (bufLen) buf[0] = '\0';
    for (auto& m : M) {
        if (!(flags & m.bit)) continue;
        unsigned used = 0; while (used < bufLen && buf[used]) used++;
        if (n > 0 && used + 2 < bufLen) { buf[used++] = ';'; buf[used++] = ' '; buf[used] = '\0'; }
        for (const char* p = m.txt; *p && used + 1 < bufLen; ++p) { buf[used++] = *p; buf[used] = '\0'; }
        n++;
    }
    return n;
}

#endif // CSI_TAMPER_DETECTOR_H
