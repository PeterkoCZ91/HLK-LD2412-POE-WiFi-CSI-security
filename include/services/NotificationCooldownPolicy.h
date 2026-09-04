#ifndef NOTIFICATION_COOLDOWN_POLICY_H
#define NOTIFICATION_COOLDOWN_POLICY_H

// Notification rate limiting, keyed by PRODUCER rather than by message type.
//
// The type alone is the wrong key because several unrelated producers share
// one. Three raise TAMPER_ALERT — the radar tamper check, the CSI tamper
// detector and anti-masking — so a single false CSI tamper used to silence a
// GENUINE radar tamper for the whole cooldown window. Four more share
// HEALTH_WARNING (disarm reminder, sensor-silent, cross-modal desync, low
// memory), where the chattiest one masked the rest.
//
// The project has hit this before and treated it as a naming problem: see the
// "FIX #18" comment in SecurityMonitor.cpp, where a zone alert was moved off
// TAMPER_ALERT for exactly this reason. That did not remove the collision, it
// moved it into HEALTH_WARNING. Keying on the producer fixes the class.
//
// A producer that does not name itself passes AlertSource::GENERIC and keeps
// the old per-type slot, so nothing that was rate-limited before becomes
// unlimited now.
//
// Arduino-free so it can be host-tested; NotificationService owns the instance.

#include <stdint.h>

enum class NotificationType {
    TAMPER_ALERT,
    PRESENCE_DETECTED,
    PRESENCE_CLEARED,
    SYSTEM_ERROR,
    WIFI_ANOMALY,
    HEALTH_WARNING,
    ALARM_STATE_CHANGE,
    ENTRY_DETECTED,
    ALARM_TRIGGERED,      // FIX #2: dedicated type for real alarm triggers (never tamper-gated)
    COUNT
};

// Who raised the alert. One entry per call site that can repeat; alerts which
// bypass the cooldown entirely (the security-critical ones) do not need one.
enum class AlertSource : uint8_t {
    GENERIC = 0,        // keeps the historical per-type slot
    RADAR_TAMPER,       // SecurityMonitor: radar reports obstruction
    CSI_TAMPER,         // SecurityMonitor: CSI sensor went blind
    ANTI_MASK,          // SecurityMonitor: no activity at all for too long
    SENSOR_SILENT,      // SecurityMonitor: health warning about silence
    CROSSMODAL,         // SecurityMonitor: radar and CSI disagree
    LOW_MEMORY,         // SecurityMonitor: free heap below the floor
    DISARM_REMINDER,    // SecurityMonitor: presence while disarmed
    LOITERING,          // SecurityMonitor: someone lingering close
    ZONE_ENTRY,         // SecurityMonitor: configured zone entered
    RADAR_OFFLINE,      // SecurityMonitor: radar stopped answering on UART
    COUNT
};

class NotificationCooldownPolicy {
public:
    uint32_t defaultCooldownMs = 300000;
    // WiFi anomalies are noisy and low-value; they have always had their own
    // much longer window and keep it.
    uint32_t wifiAnomalyCooldownMs = 7200000;

    void reset() {
        for (uint8_t i = 0; i < SLOT_COUNT; i++) { _last[i] = 0; _stamped[i] = false; }
    }

    // True if this producer may send now.
    bool allow(uint32_t nowMs, NotificationType type, AlertSource source) const {
        uint8_t s = _slot(type, source);
        if (!_stamped[s]) return true;          // never sent -> always allowed
        return (uint32_t)(nowMs - _last[s]) >= _required(type);
    }

    // Record a successful send. Kept separate from allow() so a send that
    // fails does not start the cooldown.
    void stamp(uint32_t nowMs, NotificationType type, AlertSource source) {
        uint8_t s = _slot(type, source);
        _last[s] = nowMs;
        _stamped[s] = true;                     // explicit flag: millis() is 0 for real once
    }

private:
    static constexpr uint8_t NUM_TYPES  = (uint8_t)NotificationType::COUNT;
    static constexpr uint8_t NUM_NAMED  = (uint8_t)AlertSource::COUNT - 1;
    static constexpr uint8_t SLOT_COUNT = NUM_TYPES + NUM_NAMED;

    // GENERIC keeps one slot per type; every named producer gets its own,
    // independent of the type it happens to raise.
    static uint8_t _slot(NotificationType type, AlertSource source) {
        if (source == AlertSource::GENERIC) return (uint8_t)type;
        return NUM_TYPES + (uint8_t)source - 1;
    }

    uint32_t _required(NotificationType type) const {
        return type == NotificationType::WIFI_ANOMALY ? wifiAnomalyCooldownMs
                                                      : defaultCooldownMs;
    }

    uint32_t _last[SLOT_COUNT] = {0};
    bool     _stamped[SLOT_COUNT] = {false};
};

#endif // NOTIFICATION_COOLDOWN_POLICY_H
