#ifndef CSI_EVENT_RING_H
#define CSI_EVENT_RING_H

#include <stdint.h>

// P1.3 RAM event ring buffer (navrh sekce 17.2): keep the last N diagnostic
// events in RAM only — never write per-second samples to flash. We record just
// edges, spikes, model disagreements and health changes, so the question "why
// didn't the alarm fire?" can be answered after the fact without permanent
// external second-by-second logging. Pure, header-only, native-testable.

enum class CsiEventType : uint8_t {
    MOTION_ENTER = 0,       // active detector went idle -> motion
    MOTION_EXIT,            // active detector went motion -> idle
    VARIANCE_SPIKE,         // variance far above the effective threshold
    MODEL_DISAGREEMENT,     // active vs candidate (shadow) verdict differ (P1.1)
    HEALTH_CHANGE,          // sensor health flags changed (P1.4)
};

inline const char* csiEventTypeStr(CsiEventType t) {
    switch (t) {
        case CsiEventType::MOTION_ENTER:       return "motion_enter";
        case CsiEventType::MOTION_EXIT:        return "motion_exit";
        case CsiEventType::VARIANCE_SPIKE:     return "variance_spike";
        case CsiEventType::MODEL_DISAGREEMENT: return "model_disagreement";
        case CsiEventType::HEALTH_CHANGE:      return "health_change";
    }
    return "unknown";
}

struct CsiEvent {
    uint32_t     seq = 0;            // monotonic, assigned by the ring on push
    uint32_t     uptimeMs = 0;
    CsiEventType type = CsiEventType::MOTION_ENTER;
    float        variance = 0.0f;
    float        effectiveThreshold = 0.0f;
    float        shadowThreshold = 0.0f;    // candidate effective threshold (P1.1 shadow)
    bool         activeMotion = false;
    bool         shadowMotion = false;      // candidate verdict (P1.1 shadow)
    bool         radarPresent = false;
    float        mlProbability = 0.0f;
    int16_t      rssi = 0;
    float        pps = 0.0f;
    uint16_t     healthFlags = 0;           // P1.4 flags snapshot (0 for non-health events)
};

// Fixed-capacity ring. Oldest entries are overwritten once full. Sequence
// numbers stay monotonic across clear() so `after_seq` pagination is stable.
template <uint16_t CAP>
class CsiEventRing {
public:
    // Append an event; the ring assigns and returns its sequence number.
    uint32_t push(CsiEvent e) {
        e.seq = _nextSeq++;
        _buf[_head] = e;
        _head = (uint16_t)((_head + 1) % CAP);
        if (_count < CAP) _count++;
        return e.seq;
    }

    // Drop all stored events but keep the sequence counter monotonic.
    void clear() { _head = 0; _count = 0; }

    uint16_t count()   const { return _count; }
    uint16_t capacity()const { return CAP; }
    uint32_t lastSeq() const { return _nextSeq - 1; }  // 0 before the first push

    // Copy up to `limit` events with seq > afterSeq into out[], oldest-first.
    // Returns the number written (bounded by limit, outCap and stored count).
    uint16_t query(uint32_t afterSeq, uint16_t limit, CsiEvent* out, uint16_t outCap) const {
        uint16_t written = 0;
        uint16_t oldest = (uint16_t)((_head + CAP - _count) % CAP);
        for (uint16_t i = 0; i < _count; i++) {
            const CsiEvent& e = _buf[(uint16_t)((oldest + i) % CAP)];
            if (e.seq <= afterSeq) continue;
            if (written >= limit || written >= outCap) break;
            out[written++] = e;
        }
        return written;
    }

private:
    CsiEvent _buf[CAP];
    uint16_t _head = 0;      // next write slot
    uint16_t _count = 0;
    uint32_t _nextSeq = 1;   // first pushed event gets seq 1
};

#endif // CSI_EVENT_RING_H
