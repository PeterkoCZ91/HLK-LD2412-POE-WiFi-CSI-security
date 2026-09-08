#ifndef HEAP_WATERMARK_RING_H
#define HEAP_WATERMARK_RING_H
// Dedicated RTC-noinit ring for HeapWatermarkTripwire findings.
//
// dev18, after a field lesson (2026-08-31): the tripwire originally wrote through
// systemLog into LogRtcRing. That ring holds 20 entries and is SHARED with
// ordinary logging, so on a node with a flapping Ethernet link the ETH handler
// turned it over roughly every 50 minutes. Three hours after deployment
// /api/logs held 20 records, all of them "ETH link" — the five lines proving
// discovery drains 33 kB were already evicted. The tripwire's own cap protected
// everyone else from it, and nothing protected it from everyone else.
//
// So: a ring nobody else writes to, sized to the tripwire's own record cap, in
// RTC-noinit memory so a panic reboot leaves the evidence intact.
// Header-only and Arduino-free, so the native tests can drive it.
// Layout change => bump HEAP_WM_RING_MAGIC; stale rings then self-invalidate.
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define HEAP_WM_RING_CAPACITY 16

// One recorded watermark drop plus the context that was live at that moment.
struct HeapWatermarkRecord {
    uint32_t uptimeS;
    uint32_t prevWatermark;   // what it dropped FROM
    uint32_t watermark;       // what it dropped TO
    uint32_t largest;         // largest free block at the time of reading
    uint32_t freeNow;         // free heap at the time of reading
    uint32_t mqttReconnects;
    int16_t  rssi;
    int16_t  discoveryIndex;  // -1 = HA discovery not running
    uint8_t  mqttConnected;
    uint8_t  runtimeOp;       // RuntimeOperation enum value
    uint8_t inFlight;
    uint8_t sseClients;
    uint32_t sseWaiting;
    uint32_t activityMask;
};

struct HeapWatermarkRtcRing {
    uint32_t magic;
    uint32_t crc;             // CRC32 over count + records
    uint32_t count;
    HeapWatermarkRecord records[HEAP_WM_RING_CAPACITY];
};

static const uint32_t HEAP_WM_RING_MAGIC = 0x48576D32;  // "HWm2": web/activity context

// Same reflected CRC32 as LogRing — bytewise, no table; this runs at most
// HEAP_WM_RING_CAPACITY times per boot.
inline uint32_t heapWmRingCrc32Buf(uint32_t crc, const uint8_t* p, size_t len) {
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
    }
    return ~crc;
}

inline uint32_t heapWmRingCrc(const HeapWatermarkRtcRing& r) {
    uint32_t crc = heapWmRingCrc32Buf(0, (const uint8_t*)&r.count, sizeof(r.count));
    return heapWmRingCrc32Buf(crc, (const uint8_t*)r.records, sizeof(r.records));
}

inline bool heapWmRingValid(const HeapWatermarkRtcRing& r) {
    return r.magic == HEAP_WM_RING_MAGIC
        && r.count <= HEAP_WM_RING_CAPACITY
        && r.crc == heapWmRingCrc(r);
}

inline void heapWmRingInit(HeapWatermarkRtcRing& r) {
    memset(&r, 0, sizeof(r));
    r.magic = HEAP_WM_RING_MAGIC;
    r.crc = heapWmRingCrc(r);
}

inline uint32_t heapWmRingCount(const HeapWatermarkRtcRing& r) { return r.count; }

// Append oldest-first. On overflow the record is DROPPED, not wrapped: the
// earliest drops after a boot carry the discovery signature, and losing those
// to a later writer is exactly the failure this ring was built to prevent.
inline void heapWmRingAppend(HeapWatermarkRtcRing& r, const HeapWatermarkRecord& e) {
    if (r.count >= HEAP_WM_RING_CAPACITY) return;
    r.records[r.count] = e;
    r.count++;
    r.crc = heapWmRingCrc(r);
}

#endif  // HEAP_WATERMARK_RING_H
