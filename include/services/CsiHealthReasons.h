#ifndef CSI_HEALTH_REASONS_H
#define CSI_HEALTH_REASONS_H

#include <stdint.h>

// P1.4 Health reason flags (navrh sekce 17.7): motion=false does NOT imply a
// healthy sensor. This pure, native-testable core turns a snapshot of subsystem
// signals into a concrete set of reasons and a 0-100 score, so the frontend /
// Home Assistant can tell a genuinely quiet room from a starved link, a missing
// model, or a threshold pinned to the absolute floor.

enum CsiHealthFlag : uint16_t {
    CSI_HEALTH_OK                    = 0,
    CSI_HEALTH_NO_HT_LTF             = 1u << 0,  // CSI active but no HT LTF frames (AP incompatible / WiFi6-only)
    CSI_HEALTH_PACKET_RATE_LOW       = 1u << 1,  // capture rate under the usable floor
    CSI_HEALTH_PACKET_RATE_UNSTABLE  = 1u << 2,  // capture rate swinging far from its own average
    CSI_HEALTH_WIFI_ROAMED           = 1u << 3,  // AP/BSSID changed recently — baseline may be mixed
    CSI_HEALTH_MODEL_MISSING         = 1u << 4,  // no valid learned/active site model
    CSI_HEALTH_MODEL_STALE           = 1u << 5,  // active model older than the staleness horizon
    CSI_HEALTH_LEARNING_CONTAMINATED = 1u << 6,  // site-learning rejecting most samples (motion/radar)
    CSI_HEALTH_RADAR_UNAVAILABLE     = 1u << 7,  // LD2412 radar not reporting
    CSI_HEALTH_MQTT_DISCONNECTED     = 1u << 8,  // MQTT enabled but not connected (reporting only)
    CSI_HEALTH_CLOCK_INVALID         = 1u << 9,  // wall clock not synced — timestamps/staleness unreliable
};

// Highest reason bit index in use — bump when adding flags.
static constexpr uint8_t CSI_HEALTH_FLAG_COUNT = 10;

// Snapshot of everything needed to judge sensor health. Filled by the caller
// from CSIService / LD2412 / MQTT state; the classifier stays pure.
struct CsiHealthInputs {
    bool  csiActive           = false;
    bool  htLtfSeen           = false;
    float packetRate          = 0.0f;
    float packetRateFloorPps  = 0.0f;   // below this → PACKET_RATE_LOW
    bool  packetRateUnstable  = false;
    bool  wifiRoamedRecently  = false;
    bool  modelValid          = false;
    bool  modelStale          = false;  // only trusted when clockValid
    bool  learningContaminated= false;
    bool  radarAvailable      = true;
    bool  mqttExpected        = false;  // MQTT configured/enabled
    bool  mqttConnected       = false;
    bool  clockValid          = false;
};

// Derive the set of active health-reason flags. No-HT-LTF is only meaningful
// while CSI is active; staleness only when the clock is valid.
inline uint16_t csiHealthReasons(const CsiHealthInputs& in) {
    uint16_t f = CSI_HEALTH_OK;
    if (in.csiActive && !in.htLtfSeen)              f |= CSI_HEALTH_NO_HT_LTF;
    if (in.packetRate < in.packetRateFloorPps)      f |= CSI_HEALTH_PACKET_RATE_LOW;
    if (in.packetRateUnstable)                      f |= CSI_HEALTH_PACKET_RATE_UNSTABLE;
    if (in.wifiRoamedRecently)                      f |= CSI_HEALTH_WIFI_ROAMED;
    if (!in.modelValid)                             f |= CSI_HEALTH_MODEL_MISSING;
    else if (in.clockValid && in.modelStale)        f |= CSI_HEALTH_MODEL_STALE;
    if (in.learningContaminated)                    f |= CSI_HEALTH_LEARNING_CONTAMINATED;
    if (!in.radarAvailable)                         f |= CSI_HEALTH_RADAR_UNAVAILABLE;
    if (in.mqttExpected && !in.mqttConnected)       f |= CSI_HEALTH_MQTT_DISCONNECTED;
    if (!in.clockValid)                             f |= CSI_HEALTH_CLOCK_INVALID;
    return f;
}

// Per-flag penalty on the 0-100 health score. Detection-blinding faults weigh
// most; reporting-only faults (MQTT, clock) weigh least.
inline uint8_t csiHealthPenalty(CsiHealthFlag flag) {
    switch (flag) {
        case CSI_HEALTH_NO_HT_LTF:             return 40;
        case CSI_HEALTH_PACKET_RATE_LOW:       return 30;
        case CSI_HEALTH_PACKET_RATE_UNSTABLE:  return 15;
        case CSI_HEALTH_WIFI_ROAMED:           return 10;
        case CSI_HEALTH_MODEL_MISSING:         return 40;
        case CSI_HEALTH_MODEL_STALE:           return 15;
        case CSI_HEALTH_LEARNING_CONTAMINATED: return 20;
        case CSI_HEALTH_RADAR_UNAVAILABLE:     return 30;
        case CSI_HEALTH_MQTT_DISCONNECTED:     return 10;
        case CSI_HEALTH_CLOCK_INVALID:         return 5;
        default:                               return 0;
    }
}

// 0-100 health score from a flag set. 100 == no faults; clamped at 0.
inline uint8_t csiHealthScore(uint16_t flags) {
    int score = 100;
    for (uint8_t i = 0; i < CSI_HEALTH_FLAG_COUNT; i++) {
        CsiHealthFlag flag = static_cast<CsiHealthFlag>(1u << i);
        if (flags & flag) score -= csiHealthPenalty(flag);
    }
    if (score < 0) score = 0;
    return static_cast<uint8_t>(score);
}

// Reason string for a single flag (matches the navrh vocabulary).
inline const char* csiHealthFlagStr(CsiHealthFlag flag) {
    switch (flag) {
        case CSI_HEALTH_NO_HT_LTF:             return "no_ht_ltf";
        case CSI_HEALTH_PACKET_RATE_LOW:       return "packet_rate_low";
        case CSI_HEALTH_PACKET_RATE_UNSTABLE:  return "packet_rate_unstable";
        case CSI_HEALTH_WIFI_ROAMED:           return "wifi_roamed";
        case CSI_HEALTH_MODEL_MISSING:         return "model_missing";
        case CSI_HEALTH_MODEL_STALE:           return "model_stale";
        case CSI_HEALTH_LEARNING_CONTAMINATED: return "learning_contaminated";
        case CSI_HEALTH_RADAR_UNAVAILABLE:     return "radar_unavailable";
        case CSI_HEALTH_MQTT_DISCONNECTED:     return "mqtt_disconnected";
        case CSI_HEALTH_CLOCK_INVALID:         return "clock_invalid";
        default:                               return "unknown";
    }
}

// v5.4: below the same usable-capture floor used for PACKET_RATE_LOW, the
// DSER/turbulence features feeding the ML head use packet-count time
// constants (e.g. ~100-packet EMA) that stretch far beyond their trained
// real-time window — the site's ml_probability saturates near 1.0 regardless
// of actual motion. Mirrors the _csiDataOk starvation gate already used for
// the fusion vote in SecurityMonitor, extended to the partial-starvation case.
inline bool csiMlVoteTrusted(float packetRate, float packetRateFloorPps) {
    return packetRate >= packetRateFloorPps;
}

// v5.3.1: debounce for health-change event logging. A flag set must hold for
// `stableTicks` consecutive feeds before it is reported — a boundary-oscillating
// flag (packet_rate_unstable flipping every tick filled the whole 256-event ring
// with health_change overnight) never stabilizes and never logs, while genuine
// transitions log exactly once. Pure, native-testable.
class CsiHealthDebounce {
public:
    explicit CsiHealthDebounce(uint8_t stableTicks = 10) : _need(stableTicks) {}

    // Feed the current flag set once per tick. Returns true when the value has
    // been stable for `stableTicks` ticks AND differs from the last reported
    // value — the caller should log a HEALTH_CHANGE event exactly then.
    bool feed(uint16_t flags) {
        if (flags == _pending) {
            if (_count < 0xFF) _count++;
        } else {
            _pending = flags;
            _count = 1;
        }
        if (_count >= _need && _pending != _reported) {
            _reported = _pending;
            return true;
        }
        return false;
    }

    // Last reported (logged) flag set; SENTINEL until the first report.
    uint16_t reported() const { return _reported; }

    static constexpr uint16_t SENTINEL = 0xFFFF;  // no real flag set (only 10 bits used)

private:
    uint8_t  _need;
    uint8_t  _count = 0;
    uint16_t _pending  = SENTINEL;
    uint16_t _reported = SENTINEL;
};

#endif // CSI_HEALTH_REASONS_H
