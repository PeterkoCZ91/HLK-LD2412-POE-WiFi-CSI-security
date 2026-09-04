#ifndef HEAP_SKIP_TRACKER_H
#define HEAP_SKIP_TRACKER_H

#include <stdint.h>

// dev19 low-heap skip accounting (bench .29, 2026-09-02).
//
// loop() drops the whole MQTT publish block — all three tiers — and the SSE
// telemetry tick when heapFreeUsable() is under HEAP_MIN_FOR_PUBLISH. On a
// disarmed bench node that is invisible; on an armed one it is a hole in the
// telemetry, and until now the only trace was a bare
//
//     [WARN] Low heap — skipping MQTT publish
//
// with no number in it and a 10 s rate limit in front of it. Ten lines over
// 37 h could equally have been ten events or ten thousand, and nothing recorded
// how deep the heap actually went — the STAB heartbeat five seconds either side
// reported 59 kB free, so the dip lives and dies between two samples.
//
// Hence the split: the counter is the measurement and the log line is only the
// notification. Rate-limiting the second must never quiet the first, and the
// lowest reading has to survive being suppressed, because a burst is exactly
// where the deepest dip hides.
static const uint32_t HEAP_SKIP_NO_READING = 0xFFFFFFFFu;

class HeapSkipTracker {
public:
    explicit HeapSkipTracker(uint32_t logIntervalMs = 10000)
        : _logIntervalMs(logIntervalMs) {}

    // Records one skip. Returns true when the caller should emit a log line.
    bool record(uint32_t nowMs, uint32_t freeBytes) {
        _skips++;
        if (freeBytes < _lowestFree) _lowestFree = freeBytes;
        _lastSkipMs = nowMs;

        // Unsigned subtraction so the 49,7-day millis() wrap still yields the
        // true elapsed time; a signed compare would go quiet until reboot.
        if (_logged && (uint32_t)(nowMs - _lastLogMs) < _logIntervalMs) return false;

        _lastLogMs = nowMs;
        _logged++;
        return true;
    }

    uint32_t skips()      const { return _skips; }
    uint32_t logged()     const { return _logged; }
    uint32_t suppressed() const { return _skips - _logged; }
    uint32_t lowestFree() const { return _lowestFree; }
    uint32_t lastSkipMs() const { return _lastSkipMs; }
    bool     everSkipped() const { return _skips != 0; }

private:
    uint32_t _logIntervalMs;
    uint32_t _skips      = 0;
    uint32_t _logged     = 0;
    uint32_t _lowestFree = HEAP_SKIP_NO_READING;
    uint32_t _lastLogMs  = 0;
    uint32_t _lastSkipMs = 0;
};

#endif // HEAP_SKIP_TRACKER_H
