#ifndef HEAP_WATERMARK_TRIPWIRE_H
#define HEAP_WATERMARK_TRIPWIRE_H

#include <stdint.h>

// dev17 heap-spike forensics (field, 2026-08-31: min_free_8bit = 7 700 B on a
// node whose heap median was 48 200 B — 6,6 kB below the gate's close threshold).
//
// The spike is invisible from outside. A 15 s sampler never saw the heap below
// 29 504 B, so whatever takes those ~40 kB takes them and gives them back inside
// one sampling period, and polling faster over HTTP is self-defeating: the
// request allocates more than the thing being measured.
//
// heap_caps_get_minimum_free_size() already records the low-water mark for us —
// what is missing is WHEN it moved and what was running at that moment. So the
// board polls its own watermark and writes one log line each time it drops.
//
// The constraint that shapes this class: LogRtcRing holds 20 entries TOTAL and
// is shared with ordinary logging. A tripwire that fires on every 200 B ratchet
// would evict the evidence it exists to collect. Hence a minimum drop worth a
// slot, and a hard cap on how many slots it may ever take.
class HeapWatermarkTripwire {
public:
    explicit HeapWatermarkTripwire(uint32_t minDropBytes = 2048,
                                   uint16_t maxRecords = 16)
        : _minDropBytes(minDropBytes), _maxRecords(maxRecords) {}

    // Feed the current watermark. Returns true when this drop deserves a log
    // line; the caller then reads previousWatermark()/lastRecorded() to build it.
    bool evaluate(uint32_t minFreeNow) {
        if (!_seeded) {                      // first reading = baseline
            _seeded = true;
            _baseline = minFreeNow;
            return false;
        }

        // Monotonic within a boot, so a rise means our baseline is stale (a
        // caller reset, a counter wrap) — re-anchor quietly, never log it as a
        // negative drop.
        if (minFreeNow >= _baseline) {
            _baseline = minFreeNow;
            return false;
        }

        // Measured against the last RECORDED level, not the last seen one, so a
        // slow ratchet of sub-threshold steps still adds up to one honest line.
        if (_baseline - minFreeNow < _minDropBytes) return false;
        if (_records >= _maxRecords) return false;

        _previous = _baseline;
        _baseline = minFreeNow;
        _lastRecorded = minFreeNow;
        _records++;
        return true;
    }

    uint32_t previousWatermark() const { return _previous; }
    uint32_t lastRecorded()      const { return _lastRecorded; }
    uint16_t recordCount()       const { return _records; }

private:
    uint32_t _minDropBytes;
    uint16_t _maxRecords;
    uint32_t _baseline     = 0;
    uint32_t _previous     = 0;
    uint32_t _lastRecorded = 0;
    uint16_t _records      = 0;
    bool     _seeded       = false;
};

#endif  // HEAP_WATERMARK_TRIPWIRE_H
