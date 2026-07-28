#ifndef LOG_RING_H
#define LOG_RING_H
// Pure (no Arduino) log ring shared by LogService (RAM buffer) and the
// RTC-noinit mirror that survives panic / software reset — not power loss.
// Header-only so it is testable in the native env (test_build_src = no).
// Layout change => bump LOG_RING_MAGIC, stale mirrors then self-invalidate.
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define LOG_RING_CAPACITY 20

struct LogEntry {
    uint32_t timestamp;   // seconds since boot
    char type[8];         // INFO, WARN, ERROR, ALARM
    char message[80];
    uint8_t prev_boot;    // 1 = restored from previous boot's RTC mirror
};

struct LogRtcRing {
    uint32_t magic;       // LOG_RING_MAGIC
    uint32_t crc;         // CRC32 over head+count+entries
    uint32_t head;        // index of oldest entry
    uint32_t count;
    LogEntry entries[LOG_RING_CAPACITY];
};

static const uint32_t LOG_RING_MAGIC = 0x4C52694E;  // "LRiN" v1

// Standard CRC32 (reflected, poly 0xEDB88320), bytewise — no table; the ring
// is tiny and this runs once per log line.
inline uint32_t logRingCrc32Buf(uint32_t crc, const uint8_t* p, size_t len) {
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
    }
    return ~crc;
}

inline uint32_t logRingCrc(const LogRtcRing& r) {
    uint32_t crc = logRingCrc32Buf(0, (const uint8_t*)&r.head, sizeof(r.head));
    crc = logRingCrc32Buf(crc, (const uint8_t*)&r.count, sizeof(r.count));
    crc = logRingCrc32Buf(crc, (const uint8_t*)r.entries, sizeof(r.entries));
    return crc;
}

inline bool logRingValid(const LogRtcRing& r) {
    return r.magic == LOG_RING_MAGIC
        && r.head < LOG_RING_CAPACITY
        && r.count <= LOG_RING_CAPACITY
        && r.crc == logRingCrc(r);
}

inline void logRingInit(LogRtcRing& r) {
    memset(&r, 0, sizeof(r));
    r.magic = LOG_RING_MAGIC;
    r.crc = logRingCrc(r);
}

inline void logRingAppend(LogRtcRing& r, const LogEntry& e) {
    uint32_t index = (r.head + r.count) % LOG_RING_CAPACITY;
    if (r.count < LOG_RING_CAPACITY) {
        r.count++;
    } else {
        index = r.head;
        r.head = (r.head + 1) % LOG_RING_CAPACITY;
    }
    r.entries[index] = e;
    r.crc = logRingCrc(r);
}

// Copies entries oldest-first into out (up to outCap), marking prev_boot=1.
// Returns the number of entries copied.
inline uint32_t logRingSnapshot(const LogRtcRing& r, LogEntry* out, uint32_t outCap) {
    uint32_t n = (r.count < outCap) ? r.count : outCap;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = r.entries[(r.head + i) % LOG_RING_CAPACITY];
        out[i].prev_boot = 1;
    }
    return n;
}

#endif
