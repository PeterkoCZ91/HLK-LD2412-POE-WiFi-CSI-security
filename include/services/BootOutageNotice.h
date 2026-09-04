#ifndef BOOT_OUTAGE_NOTICE_H
#define BOOT_OUTAGE_NOTICE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// dev8: proactive Telegram notice after a dirty boot. Ten panics over three
// August days went unnoticed until someone read reset_history by hand — the
// node knows everything about its own outage at the next boot (reset reason,
// oom_gate marker, pre-crash heap from safeRestart NVS keys, coredump flag),
// so it reports itself instead of waiting to be audited.
//
// Pure and native-testable; main.cpp builds the message in setup() and a loop
// one-shot delivers it once the network is up.

struct BootOutageInfo {
    const char* resetReason = "";     // human string ("Exception/Panic", ...)
    const char* restartCause = "";    // prev restart_cause ("none", "oom_gate heap=..", ...)
    const char* fwVersion = "";       // firmware that is reporting the outage
    uint32_t uptimeS = 0;             // uptime before the outage
    uint32_t prevHeap = 0;            // NVS last_heap (0 = never recorded)
    uint32_t prevMaxAlloc = 0;
    uint32_t prevMinHeap = 0;
    bool coredumpPresent = false;
};

// A boot is "dirty" (worth a notice) when the previous run ended in a crash
// (panic / any watchdog / brownout) or in an oom_gate restart — that one is a
// controlled esp_restart(), so only the cause string carries the incident.
// Power-on stays quiet: smart-plug power-cycles are the normal reboot path on
// PoE sites and cannot be told apart from a power outage.
inline bool bootOutageIsDirty(const char* resetReason, const char* restartCause) {
    if (restartCause && strncmp(restartCause, "oom_gate", 8) == 0) return true;
    if (!resetReason) return false;
    return strcmp(resetReason, "Exception/Panic") == 0 ||
           strcmp(resetReason, "Interrupt WDT") == 0 ||
           strcmp(resetReason, "Task WDT") == 0 ||
           strcmp(resetReason, "Other WDT") == 0 ||
           strcmp(resetReason, "Brownout") == 0;
}

// Telegram-ready plain-text message. Returns snprintf-style length (may exceed
// cap; output is always NUL-terminated within cap). Sections with no data
// (heap never recorded, cause "none") are omitted rather than printed empty.
inline int formatBootOutageNotice(char* out, size_t cap, const BootOutageInfo& info) {
    uint32_t d = info.uptimeS / 86400;
    uint32_t h = (info.uptimeS % 86400) / 3600;
    uint32_t m = (info.uptimeS % 3600) / 60;
    char up[32];
    if (d > 0) snprintf(up, sizeof(up), "%lud %luh %lum",
                        (unsigned long)d, (unsigned long)h, (unsigned long)m);
    else       snprintf(up, sizeof(up), "%luh %lum",
                        (unsigned long)h, (unsigned long)m);

    int n = snprintf(out, cap, "\xF0\x9F\x94\xB4 Node outage detected (previous run)\n"
                               "firmware: %s\n"
                               "reason: %s\n", info.fwVersion ? info.fwVersion : "?",
                               info.resetReason ? info.resetReason : "?");
    size_t off = (n < 0 || (size_t)n >= cap) ? (cap ? cap - 1 : 0) : (size_t)n;

    if (info.restartCause && info.restartCause[0] != '\0' &&
        strcmp(info.restartCause, "none") != 0) {
        int k = snprintf(out + off, cap - off, "cause: %s\n", info.restartCause);
        if (k > 0) { n += k; off = ((size_t)k >= cap - off) ? cap - 1 : off + (size_t)k; }
    }

    int k = snprintf(out + off, cap - off, "uptime before: %s\n", up);
    if (k > 0) { n += k; off = ((size_t)k >= cap - off) ? cap - 1 : off + (size_t)k; }

    if (info.prevHeap > 0) {
        k = snprintf(out + off, cap - off, "heap before (free/maxalloc/min): %lu/%lu/%lu\n",
                     (unsigned long)info.prevHeap, (unsigned long)info.prevMaxAlloc,
                     (unsigned long)info.prevMinHeap);
        if (k > 0) { n += k; off = ((size_t)k >= cap - off) ? cap - 1 : off + (size_t)k; }
    }

    k = snprintf(out + off, cap - off, "coredump: %s", info.coredumpPresent ? "yes" : "no");
    if (k > 0) n += k;
    return n;
}

// Machine-readable MQTT payload for the same incident — published NON-retained
// on security/<id>/system/outage so a Home Assistant automation can forward it
// (e.g. to Telegram) without replaying a stale event after an HA restart.
// Reason/cause strings are firmware-generated (no user input, no quotes), so
// no JSON escaping is needed. All fields always present; "none" cause → "".
inline int formatBootOutageEventJson(char* out, size_t cap, const BootOutageInfo& info) {
    const char* cause = info.restartCause ? info.restartCause : "";
    if (strcmp(cause, "none") == 0) cause = "";
    return snprintf(out, cap,
        "{\"v\":1,\"event\":\"boot_outage\",\"fw_version\":\"%s\","
        "\"reason\":\"%s\",\"cause\":\"%s\","
        "\"uptime_s\":%lu,\"heap_free\":%lu,\"heap_maxalloc\":%lu,\"heap_min\":%lu,"
        "\"coredump\":%s}",
        info.fwVersion ? info.fwVersion : "?", info.resetReason ? info.resetReason : "?", cause,
        (unsigned long)info.uptimeS, (unsigned long)info.prevHeap,
        (unsigned long)info.prevMaxAlloc, (unsigned long)info.prevMinHeap,
        info.coredumpPresent ? "true" : "false");
}

// Survived heap-pressure episode (dev7 gate closed → reopened), same topic.
inline int formatHeapGateEventJson(char* out, size_t cap, uint32_t closedS,
                                   uint32_t rejectsTotal, uint32_t heapFree, uint32_t heapMin) {
    return snprintf(out, cap,
        "{\"v\":1,\"event\":\"heap_gate_episode\",\"closed_s\":%lu,"
        "\"rejects_total\":%lu,\"heap_free\":%lu,\"heap_min\":%lu}",
        (unsigned long)closedS, (unsigned long)rejectsTotal,
        (unsigned long)heapFree, (unsigned long)heapMin);
}

#endif // BOOT_OUTAGE_NOTICE_H
