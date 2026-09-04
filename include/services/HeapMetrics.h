#ifndef HEAP_METRICS_H
#define HEAP_METRICS_H

#include <stdint.h>

// dev12 heap capability fix (field root cause 2026-08-20, a production node:
// nine oom_gate restarts in 6.3 h, /api/health reporting 78-87 kB free the
// whole time, dev7 web heap gate never closing once).
//
// arduino-esp32 implements ESP.getFreeHeap(), ESP.getMaxAllocHeap() and
// ESP.getMinFreeHeap() over MALLOC_CAP_INTERNAL (cores/esp32/Esp.cpp). On
// ESP32 that capability also covers the leftover IRAM-only heap region — for
// this firmware 0x40096000..0x400A0000, five 0x2000 pages after _iram_end,
// 40960 B raw and 40948 B after multi_heap overhead. IDF gives that region
// MALLOC_CAP_INTERNAL|MALLOC_CAP_EXEC|MALLOC_CAP_32BIT and deliberately NOT
// MALLOC_CAP_8BIT (heap/port/esp32/memory_layout.c), because it is not
// byte-addressable. malloc() and operator new can therefore never use it.
//
// So every threshold in this firmware was silently inflated by ~42 kB: the
// gate compared 28 kB against a number with a ~43 kB floor, HEAP_MIN_FOR_PUBLISH
// compared 20 kB against the same floor, and the OOM marker recorded
// "heap=45892/40948" at the exact moment a throwing `new` could not find a
// single byte. All nine field markers carried largestInternal == 40948 to the
// byte, which is that whole region sitting free while the usable heap was gone.
//
// Rule: a memory *decision* reads the byte-addressable heap. MALLOC_CAP_INTERNAL
// totals are diagnostics, published alongside so the two can be compared, never
// compared against a threshold.

struct HeapReading {
    uint32_t freeUsable      = 0;  // MALLOC_CAP_8BIT — what new/malloc may get
    uint32_t largestUsable   = 0;  // MALLOC_CAP_8BIT largest contiguous block
    uint32_t minFreeUsable   = 0;  // MALLOC_CAP_8BIT low-water mark since boot
    uint32_t freeInternal    = 0;  // MALLOC_CAP_INTERNAL — diagnostics only
    uint32_t largestInternal = 0;  // MALLOC_CAP_INTERNAL — diagnostics only
};

// Bytes counted by MALLOC_CAP_INTERNAL that no ordinary allocation can reach.
// Clamped: freeUsable and freeInternal come from two separate unlocked reads,
// so the usable side can momentarily read higher.
inline uint32_t heapUnusableInternalBytes(const HeapReading& r) {
    return (r.freeInternal > r.freeUsable) ? (r.freeInternal - r.freeUsable) : 0;
}

// True when the internal total overstates the usable heap by enough that a
// threshold expressed in internal bytes is meaningless. Surfaced in diagnostics
// so this class of bug is visible from the outside on any future board.
inline bool heapInternalIsMisleading(const HeapReading& r, uint32_t marginBytes = 8u * 1024u) {
    return heapUnusableInternalBytes(r) >= marginBytes;
}

#ifdef ARDUINO
#include <esp_heap_caps.h>

// Decision-grade readings. Same locking cost as the ESP.* helpers they replace.
inline uint32_t heapFreeUsable() {
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
}
inline uint32_t heapLargestUsable() {
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}
inline uint32_t heapMinFreeUsable() {
    return (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
}

// Diagnostics-only counterparts — what ESP.getFreeHeap()/getMaxAllocHeap()
// returned before dev12. Kept so /api/health can publish both and the
// inflation stays auditable in the field.
inline uint32_t heapFreeInternal() {
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}
inline uint32_t heapLargestInternal() {
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
}

inline HeapReading heapReadingNow() {
    HeapReading r;
    r.freeUsable      = heapFreeUsable();
    r.largestUsable   = heapLargestUsable();
    r.minFreeUsable   = heapMinFreeUsable();
    r.freeInternal    = heapFreeInternal();
    r.largestInternal = heapLargestInternal();
    return r;
}
#endif  // ARDUINO

#endif  // HEAP_METRICS_H
