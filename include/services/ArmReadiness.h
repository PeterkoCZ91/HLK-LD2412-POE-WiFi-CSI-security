#ifndef ARM_READINESS_H
#define ARM_READINESS_H

// Pure pre-arm health self-test (#9 silent arm-check). Answers "if I arm right
// now, is the system actually able to sense an intruder — or is it blind?".
// Arduino-free so it can be host-tested like AlarmFSM; SecurityMonitor gathers
// the live inputs and calls computeArmWarnings() at arm time.
//
// The result is a WARNING bitmask, NOT a veto: arming still proceeds. The whole
// point is to tell the user "you armed, but the radar is down" instead of arming
// silently into a blind state.

#include <cstdint>

enum ArmWarn : uint32_t {
    ARM_WARN_NONE          = 0,
    ARM_WARN_RADAR_DOWN    = 1u << 0,  // radar subsystem not delivering frames
    ARM_WARN_CSI_NO_PACKETS= 1u << 1,  // CSI compiled+enabled but packet rate ~0
    ARM_WARN_MODEL_MISSING = 1u << 2,  // no valid learned/active CSI model
    ARM_WARN_MODEL_STALE   = 1u << 3,  // active model older than the stale limit
    ARM_WARN_CLOCK_INVALID = 1u << 4,  // NTP time not set — event timestamps bogus
    ARM_WARN_MQTT_DOWN     = 1u << 5,  // MQTT wanted (HA integration) but disconnected
};

struct ArmHealthInputs {
    bool     csiCompiled   = false;  // USE_CSI build
    bool     csiEnabled    = false;  // runtime csi_enabled
    bool     csiActive     = false;  // CSIService::isActive()
    float    csiPacketRate = 0.0f;   // pps
    bool     radarOk       = false;  // radar delivering frames recently
    bool     modelValid    = false;  // active CSI model present & valid
    uint32_t modelAgeSec   = 0;      // seconds since active model created/applied
    bool     timeValid     = false;  // NTP synced
    bool     mqttWanted    = false;  // mqtt_enabled config
    bool     mqttConnected = false;
};

// A CSI model older than this is flagged stale (30 days). Long-lived on purpose:
// the site model is meant to persist, this only catches "forgot it exists for a month".
constexpr uint32_t ARM_MODEL_STALE_SEC = 30u * 24u * 3600u;
// Packet rate below this while CSI is active means the passive sensor is effectively blind.
constexpr float ARM_MIN_PACKET_RATE = 1.0f;

inline uint32_t computeArmWarnings(const ArmHealthInputs& in) {
    uint32_t w = ARM_WARN_NONE;

    if (!in.radarOk) w |= ARM_WARN_RADAR_DOWN;

    // CSI checks only apply when CSI is actually meant to be sensing.
    if (in.csiCompiled && in.csiEnabled) {
        if (!in.csiActive || in.csiPacketRate < ARM_MIN_PACKET_RATE)
            w |= ARM_WARN_CSI_NO_PACKETS;
        if (!in.modelValid)
            w |= ARM_WARN_MODEL_MISSING;
        else if (in.modelAgeSec > ARM_MODEL_STALE_SEC)
            w |= ARM_WARN_MODEL_STALE;
    }

    if (!in.timeValid) w |= ARM_WARN_CLOCK_INVALID;
    if (in.mqttWanted && !in.mqttConnected) w |= ARM_WARN_MQTT_DOWN;

    return w;
}

// Human-readable, short reason list for the arm notification (e.g. Telegram).
// Writes into `buf` (comma-separated), returns number of warnings rendered.
inline int renderArmWarnings(uint32_t w, char* buf, unsigned bufLen) {
    struct { uint32_t bit; const char* txt; } M[] = {
        { ARM_WARN_RADAR_DOWN,     "radar offline" },
        { ARM_WARN_CSI_NO_PACKETS, "CSI no packets" },
        { ARM_WARN_MODEL_MISSING,  "no CSI model" },
        { ARM_WARN_MODEL_STALE,    "CSI model stale" },
        { ARM_WARN_CLOCK_INVALID,  "clock not set" },
        { ARM_WARN_MQTT_DOWN,      "MQTT disconnected" },
    };
    int n = 0;
    if (bufLen) buf[0] = '\0';
    for (auto& m : M) {
        if (!(w & m.bit)) continue;
        unsigned used = 0;
        while (used < bufLen && buf[used]) used++;
        if (n > 0 && used + 2 < bufLen) { buf[used++] = ','; buf[used++] = ' '; buf[used] = '\0'; }
        for (const char* p = m.txt; *p && used + 1 < bufLen; ++p) { buf[used++] = *p; buf[used] = '\0'; }
        n++;
    }
    return n;
}

#endif // ARM_READINESS_H
