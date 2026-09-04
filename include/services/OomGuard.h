#ifndef OOM_GUARD_H
#define OOM_GUARD_H

#include <stdint.h>

// dev7 OOM last-resort guard (field coredump 2026-08-15): when a throwing
// `new` cannot be satisfied anywhere in the firmware (our code or
// ESPAsyncWebServer internals), libstdc++ ends in std::terminate → abort →
// panic. std::set_new_handler lets us intercept that moment; the handler is
// forbidden from allocating, so the incident record goes into an RTC-noinit
// marker (survives the software reset, not power loss — same contract as
// LogRing) and the device restarts cleanly instead of panicking. The next
// boot folds the marker into reset_history as "oom_gate".
//
// Everything here is pure and native-testable; the Arduino wiring
// (RTC_NOINIT_ATTR instance, std::set_new_handler, esp_restart) lives in
// main.cpp.

struct OomMarker {
    uint32_t magic;
    uint32_t uptimeS;
    uint32_t freeBytes;
    uint32_t largestBytes;
    uint32_t check;
};

static constexpr uint32_t OOM_MARKER_MAGIC = 0x4F4F4D31u;  // "OOM1"

// Checksum folds a constant so all-zero and pattern-filled RTC garbage can
// never validate even if magic happens to match.
inline uint32_t oomMarkerChecksum(const OomMarker& m) {
    return m.magic ^ m.uptimeS ^ m.freeBytes ^ m.largestBytes ^ 0xB5E0C0DEu;
}

inline void oomMarkerSet(OomMarker& m, uint32_t uptimeS, uint32_t freeBytes, uint32_t largestBytes) {
    m.magic        = OOM_MARKER_MAGIC;
    m.uptimeS      = uptimeS;
    m.freeBytes    = freeBytes;
    m.largestBytes = largestBytes;
    m.check        = oomMarkerChecksum(m);
}

inline void oomMarkerClear(OomMarker& m) {
    m.magic = 0;
    m.check = 0;
}

inline bool oomMarkerValid(const OomMarker& m) {
    return m.magic == OOM_MARKER_MAGIC && m.check == oomMarkerChecksum(m);
}

// Restart (true) or fall through to abort+coredump (false).
//  - restartEnabled=false: operator explicitly wants the panic dump back.
//  - rebootInhibit: OTA flash write in progress — restarting mid-write risks
//    a brick; abort at least leaves a coredump.
//  - loop guard: if the PREVIOUS boot already ended in an oom_gate restart
//    and this boot OOMs again within loopGuardS, restarting would hide an
//    infinite boot loop — abort instead so it surfaces as a panic.
inline bool oomShouldRestart(bool restartEnabled, bool rebootInhibit,
                             bool lastBootWasOomRestart, uint32_t uptimeS,
                             uint32_t loopGuardS = 60) {
    if (!restartEnabled) return false;
    if (rebootInhibit) return false;
    if (lastBootWasOomRestart && uptimeS < loopGuardS) return false;
    return true;
}

#endif // OOM_GUARD_H
