#ifndef CSI_MOTION_EDGE_H
#define CSI_MOTION_EDGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

// Passage-edge event payload (API-unification "variant S"). Published
// NON-RETAINED on <prefix>/csi/event whenever the variance-based motion state
// transitions, so Home Assistant can detect a real passage from an EDGE instead
// of interpreting the retained <prefix>/csi/motion STATE — which pins ON and is
// replayed to every new subscriber and after an HA restart.
//
//   boot_id  32-bit random hex, stable for one boot → distinguishes a live event
//            from a retained/offline-buffer replay of a previous boot.
//   seq      monotonic per boot → dedup + ordering; never resets except on reboot.
//   uptime_ms monotonic since boot → HA can reject an event older than the last
//            known state (defeats offline-buffer replay of stale edges).
//
// Pure + header-only so it is native-testable without Arduino/MQTT.

// Writes a compact JSON edge event into buf. Returns the number of chars written
// (excluding the terminating NUL), or 0 on truncation / bad args (buf untouched
// as a valid payload in that case — caller must not publish a 0-length result).
inline int formatCsiMotionEdge(char* buf, size_t n,
                               const char* bootId, uint32_t seq, bool enter,
                               uint32_t uptimeMs, const char* source,
                               float variance, float threshold) {
    if (!buf || n == 0 || !bootId || !source) return 0;
    int w = snprintf(buf, n,
        "{\"v\":1,\"boot_id\":\"%s\",\"seq\":%lu,\"event\":\"%s\","
        "\"uptime_ms\":%lu,\"source\":\"%s\",\"variance\":%.6f,\"threshold\":%.6f}",
        bootId, (unsigned long)seq, enter ? "motion_started" : "motion_ended",
        (unsigned long)uptimeMs, source, (double)variance, (double)threshold);
    if (w < 0 || (size_t)w >= n) return 0;  // encoding error or truncated
    return w;
}

#endif // CSI_MOTION_EDGE_H
