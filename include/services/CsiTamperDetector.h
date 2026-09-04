#ifndef CSI_TAMPER_DETECTOR_H
#define CSI_TAMPER_DETECTOR_H

// #5 CSI-side tamper detection. Anti-masking today only watches the radar; the
// passive CSI sensor can be blinded by pulling/covering the AP without the radar
// ever noticing. This pure, stateful detector flags two blind-states:
//   - NO_PACKETS: packet flow collapses to ~0 while Ethernet is still up (so it
//     is not just a network outage) for longer than a grace window.
//   - FROZEN:    running variance is bit-identical for a long window — a stuck
//     DMA / frozen capture returning the same frame forever.
// Arduino-free so it can be host-tested; SecurityMonitor feeds it live inputs.
//
// Two field defects (z15, 2026-08-19..22, 13 false alerts) shaped the current
// design; test/test_csi_tamper carries the regression tests.
//
//   ALIASING. The caller samples this once per 60 s but used to hand over
//   getPacketRate(), which is an instantaneous 1-second average. Two unlucky
//   dips 60 s apart looked exactly like 60 s of blindness. We now take the
//   MONOTONIC packet count and derive the average over the real interval
//   between calls, so the verdict no longer depends on the caller's cadence
//   or on which instant it happened to sample.
//
//   BOOT TRANSIENT. Ethernet comes up seconds after reset; the WiFi station
//   needs far longer to associate and start delivering CSI frames. Judged by
//   the steady-state rules, every boot looked like sabotage — which is what
//   the field log showed, one alert per boot. startupGraceMs covers the window
//   until the first packet arrives, and is bounded so a sensor that never
//   works at all is still reported.
//
// Deliberately NOT gated on WiFi association: pulling or covering the AP
// disassociates the station, which is precisely the attack this exists to
// catch. Transient disassociation is excused by duration, never by
// suppressing the check.

#include <cstdint>

enum CsiTamper : uint32_t {
    CSI_TAMPER_NONE       = 0,
    CSI_TAMPER_NO_PACKETS = 1u << 0,
    CSI_TAMPER_FROZEN     = 1u << 1,
};

struct CsiTamperInputs {
    bool     csiActive = false;    // CSI meant to be sensing right now
    bool     ethUp = false;        // Ethernet link up (rules out network outage)
    uint32_t packetCount = 0;      // monotonic CSI frames since boot
    float    variance = 0.0f;      // running variance
    uint32_t nowMs = 0;
};

class CsiTamperDetector {
public:
    // Config (ms / pps). Generous defaults — tamper is a sustained state, not a blip.
    uint32_t noPacketGraceMs = 30000;
    uint32_t frozenGraceMs   = 120000;
    uint32_t startupGraceMs  = 300000;   // before the first packet ever seen
    float    minPps          = 1.0f;

    void reset() {
        _lowPpsSinceMs = 0;
        _lastVariance = -1.0f;
        _varChangedMs = 0;
        _seen = false;
        _havePrev = false;
        _prevCount = 0;
        _prevMs = 0;
        _everSawPackets = false;
        _startSeen = false;
        _startMs = 0;
    }

    // Returns the current tamper bitmask (0 = healthy). Call periodically.
    uint32_t update(const CsiTamperInputs& in) {
        if (!in.csiActive) { reset(); return CSI_TAMPER_NONE; }

        if (!_startSeen) { _startMs = in.nowMs; _startSeen = true; }

        uint32_t flags = CSI_TAMPER_NONE;

        // --- packet collapse, averaged over the real interval between calls ---
        // A single call carries no rate on its own; the first one only seeds
        // the baseline. Afterwards the count delta over the elapsed time is a
        // true average, so a momentary dip cannot masquerade as an outage and
        // a trickle too slow to see the room still counts as one.
        if (_havePrev) {
            uint32_t deltaMs = in.nowMs - _prevMs;
            uint32_t deltaPackets = in.packetCount - _prevCount;
            if (deltaPackets > 0) _everSawPackets = true;
            if (deltaMs > 0) {
                float pps = (float)deltaPackets * 1000.0f / (float)deltaMs;
                if (in.ethUp && pps < minPps) {
                    if (_lowPpsSinceMs == 0) _lowPpsSinceMs = in.nowMs ? in.nowMs : 1;
                } else {
                    _lowPpsSinceMs = 0;
                }
            }
        }
        _prevCount = in.packetCount;
        _prevMs = in.nowMs;
        _havePrev = true;

        // Until the sensor has proven it can deliver a frame, a quiet capture
        // is a cold start rather than a blinded one — but only for so long.
        const bool warmedUp = _everSawPackets ||
                              (uint32_t)(in.nowMs - _startMs) >= startupGraceMs;

        if (warmedUp && _lowPpsSinceMs != 0 &&
            (uint32_t)(in.nowMs - _lowPpsSinceMs) >= noPacketGraceMs) {
            flags |= CSI_TAMPER_NO_PACKETS;
        }

        // --- frozen variance ---
        if (!_seen || in.variance != _lastVariance) {
            _lastVariance = in.variance;
            _varChangedMs = in.nowMs;
            _seen = true;
        } else if (warmedUp &&
                   (uint32_t)(in.nowMs - _varChangedMs) >= frozenGraceMs) {
            flags |= CSI_TAMPER_FROZEN;
        }

        return flags;
    }

private:
    uint32_t _lowPpsSinceMs = 0;   // 0 = flow currently ok
    float    _lastVariance = -1.0f;
    uint32_t _varChangedMs = 0;
    bool     _seen = false;
    uint32_t _prevCount = 0;       // previous sample, for interval averaging
    uint32_t _prevMs = 0;
    bool     _havePrev = false;
    bool     _everSawPackets = false;
    uint32_t _startMs = 0;
    bool     _startSeen = false;
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
