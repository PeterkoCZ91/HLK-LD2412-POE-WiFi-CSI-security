#ifndef HEAP_GATE_POLICY_H
#define HEAP_GATE_POLICY_H

#include <stdint.h>

// dev7 low-heap accept gate (field coredump 2026-08-15): async_tcp died with
// an OOM abort inside ESPAsyncWebServer's request-header parsing — the library
// allocates with throwing `new`, so once the heap collapses ANY freshly
// accepted connection panics the device. This pure policy decides, per
// incoming connection, whether the web server may take on new work.
//
// Hysteresis: the gate CLOSES when free heap or the largest allocatable block
// drops under the close thresholds, and only REOPENS once both recover above
// the (higher) open thresholds — a heap hovering at the boundary must not
// flap the gate. Rejected connections are counted, never queued.
//
// Cross-task note: shouldAccept() runs on async_tcp, readers (health JSON,
// loop-task transition logging) elsewhere. All fields are 32-bit aligned
// scalars — torn reads are impossible on ESP32; counters may lag a reader by
// one connection, which is harmless for diagnostics.
// dev12 recalibration: the dev7 values (close 28 kB / open 40 kB, largest
// 12/16 kB) were derived from ESP.getFreeHeap(), which counts ~42 kB of
// IRAM-only heap this firmware can never allocate from (see HeapMetrics.h).
// They therefore sat above the node's entire usable-heap band and the gate
// could not close — a production node logged nine oom_gate restarts in
// 6.3 h with close_count 0.
// These thresholds are byte-addressable-heap bytes: the node's normal working
// band is ~36-45 kB free / ~15-20 kB largest, and the field OOMs struck
// between 1.4 and 13 kB free, so closing starts above that whole range.
//
// Release 5.7.1 recalibration (bench dev6, 2026-09-06): the "~36-45 kB
// normal" band above is stale for this build. A clean /api/restart with no
// web load showed free heap at 46 kB before MQTT connects, dropping to and
// permanently settling at ~32 kB once MQTT connects (PubSubClient/socket
// state held for the connection's life, not a leak — see
// docs/RELEASE_5.7.1_VALIDATION.md). At the OLD close threshold of 14 kB,
// that left WebAdmissionPolicy only ~1.3 kB of slack over its own admitted
// reserve at the node's real steady-state baseline — a short bounded HTTP/SSE
// stress test still triggered an oom_gate restart even with maxInFlight=4 /
// reserveBytes=8 KiB. Raised close/open thresholds by 6 kB each to restore a
// real margin against the measured ~32 kB baseline (this now admits only
// ~1 concurrent request at that baseline instead of ~2 — verify against a
// fresh stress run before assuming it holds).
struct HeapGateConfig {
    bool     enabled           = true;
    uint32_t closeFreeBytes    = 20 * 1024;
    uint32_t openFreeBytes     = 28 * 1024;
    uint32_t closeLargestBytes = 6 * 1024;
    uint32_t openLargestBytes  = 10 * 1024;
};

class HeapGatePolicy {
public:
    HeapGatePolicy() {}
    explicit HeapGatePolicy(const HeapGateConfig& cfg) : _cfg(cfg) {}

    void configure(const HeapGateConfig& cfg) { _cfg = cfg; }
    const HeapGateConfig& config() const { return _cfg; }

    // One incoming connection: returns true if the server may accept it.
    // Updates the open/closed state and the reject/episode counters.
    bool shouldAccept(uint32_t freeBytes, uint32_t largestBytes) {
        if (!_cfg.enabled) return true;
        _transition(freeBytes, largestBytes);
        if (_closed) {
            _rejectsTotal++;
            return false;
        }
        return true;
    }

    // Heap check without a connection: updates the open/closed state and the
    // episode counter but never counts a rejection. Fed by the loop task once
    // a second so a collapsing heap closes the gate (and gets logged) even
    // before the first connection arrives.
    void probe(uint32_t freeBytes, uint32_t largestBytes) {
        if (!_cfg.enabled) return;
        _transition(freeBytes, largestBytes);
    }

    bool isClosed() const { return _closed; }
    uint32_t rejectsTotal() const { return _rejectsTotal; }
    uint32_t closeCount() const { return _closeCount; }

private:
    void _transition(uint32_t freeBytes, uint32_t largestBytes) {
        if (!_closed) {
            if (freeBytes < _cfg.closeFreeBytes || largestBytes < _cfg.closeLargestBytes) {
                _closed = true;
                _closeCount++;
            }
        } else {
            if (freeBytes > _cfg.openFreeBytes && largestBytes > _cfg.openLargestBytes) {
                _closed = false;
            }
        }
    }

    HeapGateConfig _cfg;
    bool     _closed = false;
    uint32_t _rejectsTotal = 0;
    uint32_t _closeCount = 0;
};

#endif // HEAP_GATE_POLICY_H
