#ifndef WEB_ADMISSION_POLICY_H
#define WEB_ADMISSION_POLICY_H

#include <stdint.h>

// dev17 concurrent-request admission control (bench, 2026-08-31).
//
// WHAT THE HEAP GATE COULD NOT SEE
// --------------------------------
// HeapGatePolicy already runs in the accept path (GatedWebServer.h), before a
// single byte is allocated for the connection, and it is alloc-free. It still
// let the node die under N parallel /api/health requests:
//
//   N <= 10  no reaction at all, web_gate.close_count = 0
//   N == 12  gate closed, 2 connections refused, node survived
//   N == 15  gate closed, node survived once — then panicked
//            "Exception/Panic (oom_gate heap=2040/1012)"
//   N == 20  "Software reset (oom_gate heap=8660/3572)"
//   last forensic line before the panic: heapmin 27560->5972 lg=6644 fr=7640
//   (7640 B free, 6644 B largest — the heap was gone, not fragmented)
//
// The gate is a LEVEL check on a LAGGING signal. A connection costs almost
// nothing at accept time; its real price — the request object, the header and
// Digest-authorization Strings, the JsonDocument, the ~3.9 kB response buffer
// and the lwIP send buffers — is paid milliseconds later, after the whole
// accept burst has already been drained by the async_tcp task in one pass.
// Every one of the 20 accepts therefore read the same healthy ~45 kB free and
// was admitted; the heap collapsed afterwards, and the library's *throwing*
// allocations (String growth, `new AsyncWebHeader`, beginResponse) then hit an
// empty heap and aborted. Nothing in the firmware bounded how many connections
// could be admitted between two heap readings.
//
// WHAT THIS ADDS
// --------------
// Admission accounting for memory that is COMMITTED but NOT YET SPENT:
//   1. a hard ceiling on concurrently admitted connections (maxInFlight), and
//   2. a headroom rule — every connection already admitted, plus the one being
//      decided, must still be able to spend reserveBytes without pushing free
//      heap under floorBytes.
// Rule 2 makes the ceiling adaptive: at the node's normal ~45 kB free the cap
// binds, and when CSI/MQTT/an ETH flap has already eaten the heap the policy
// stops admitting long before the cap is reached.
//
// reserveBytes is calibrated from the bench numbers above: 12 concurrent
// requests moved free heap from ~45 kB to under the 14 kB close threshold,
// i.e. ~2.6 kB of resident cost per in-flight connection.
//
// WHY SLOTS AND NOT A PLAIN COUNTER
// ---------------------------------
// The completion signal is best-effort. AsyncWebServerRequest::abort() — which
// this firmware's own OOM guards call in WebRoutes.cpp — reaches lwIP's
// tcp_abort() without ever running the disconnect callback chain directly; the
// notification comes back only as an AsyncTCP event packet allocated with
// `new (std::nothrow)`, and under heap exhaustion that allocation is exactly
// what fails. A missed completion must therefore never wedge the server
// permanently, so each admission takes a timestamped slot that is reclaimed
// after slotTtlMs whether or not anyone released it.
//
// Slots carry a generation counter. A connection whose slot was reclaimed by
// the TTL can still disconnect later; without the generation its stale release
// would free a slot that meanwhile belongs to a different connection.
//
// Cross-task note: admit()/release() both run on the async_tcp task (accept
// callback and disconnect callback), readers (health JSON) elsewhere. All
// fields are 32-bit aligned scalars or byte arrays — torn reads are impossible
// on ESP32 and a counter that lags a reader by one connection is harmless for
// diagnostics. Same model as HeapGatePolicy.

enum class WebAdmit : uint8_t {
    Accept = 0,
    RejectHeapGate,     // HeapGatePolicy is closed — heap already at the floor
    RejectConcurrency,  // maxInFlight reached
    RejectReserve,      // free heap cannot cover one more connection's reserve
};

struct WebAdmissionConfig {
    bool     enabled      = true;
    uint8_t  maxInFlight  = 8;          // browsers cap at 6 sockets/host; 12 hurt the node
    uint32_t reserveBytes = 3u * 1024;  // resident heap cost of one in-flight request
    uint32_t floorBytes   = 14u * 1024; // kept in step with HeapGateConfig::closeFreeBytes
    uint32_t slotTtlMs    = 10000;      // reclaim when the disconnect never arrives
};

struct WebAdmitResult {
    WebAdmit decision = WebAdmit::Accept;
    uint16_t slot    = 0xFFFF;  // pass to release() when the connection ends
    bool accepted() const { return decision == WebAdmit::Accept; }
};

class WebAdmissionPolicy {
public:
    static constexpr uint8_t  kSlots   = 16;
    static constexpr uint16_t kNoToken = 0xFFFF;

    WebAdmissionPolicy() { _clear(); }
    explicit WebAdmissionPolicy(const WebAdmissionConfig& cfg) : _cfg(cfg) {
        _clampCap();
        _clear();
    }

    void configure(const WebAdmissionConfig& cfg) { _cfg = cfg; _clampCap(); }
    const WebAdmissionConfig& config() const { return _cfg; }

    // Keep the admission floor in step with the heap gate's close threshold so
    // an operator retuning the gate (NVS wg8_close) moves both together.
    void setFloorBytes(uint32_t bytes) { _cfg.floorBytes = bytes; }

    // One incoming connection. `gateClosed` is HeapGatePolicy's verdict, which
    // has already been evaluated and counted by the caller.
    WebAdmitResult admit(uint32_t nowMs, uint32_t freeBytes, bool gateClosed) {
        WebAdmitResult r;
        if (!_cfg.enabled) return r;  // accept, untracked (slot stays kNoToken)

        _expire(nowMs);

        if (gateClosed) {
            r.decision = WebAdmit::RejectHeapGate;
            return r;
        }
        if (_inFlight >= _cfg.maxInFlight) {
            _rejectsConcurrency++;
            r.decision = WebAdmit::RejectConcurrency;
            return r;
        }
        // The burst that killed the node passed a plain "free > floor" test on
        // every single accept. Charge the connections already admitted too.
        const uint64_t need = (uint64_t)_cfg.floorBytes +
                              (uint64_t)(_inFlight + 1) * (uint64_t)_cfg.reserveBytes;
        if ((uint64_t)freeBytes < need) {
            _rejectsReserve++;
            r.decision = WebAdmit::RejectReserve;
            return r;
        }

        const uint16_t slot = _take(nowMs);
        if (slot == kNoToken) {  // cap and slot table disagree — refuse, don't guess
            _rejectsConcurrency++;
            r.decision = WebAdmit::RejectConcurrency;
            return r;
        }
        r.slot = slot;
        return r;
    }

    // Connection finished. Safe against kNoToken, double release, and a slot
    // whose slot was already reclaimed by the TTL and handed to someone else.
    void release(uint16_t slot) {
        const uint8_t idx = (uint8_t)(slot & 0x00FF);
        const uint8_t gen = (uint8_t)(slot >> 8);
        if (idx >= kSlots) return;
        if (!_busy[idx]) return;
        if (_gen[idx] != gen) return;  // stale: slot was recycled after a TTL reclaim
        _busy[idx] = false;
        if (_inFlight) _inFlight--;
        _releasedTotal++;
    }

    // Reclaim slots whose completion signal never arrived. Also reachable from
    // a caller that wants the counters fresh without deciding on a connection.
    void expire(uint32_t nowMs) { if (_cfg.enabled) _expire(nowMs); }

    uint8_t  inFlight() const           { return _inFlight; }
    uint8_t  peakInFlight() const       { return _peakInFlight; }
    uint32_t admittedTotal() const      { return _admittedTotal; }
    uint32_t releasedTotal() const      { return _releasedTotal; }
    uint32_t rejectsConcurrency() const { return _rejectsConcurrency; }
    uint32_t rejectsReserve() const     { return _rejectsReserve; }
    uint32_t slotsExpired() const       { return _slotsExpired; }

private:
    void _clear() {
        for (uint8_t i = 0; i < kSlots; i++) {
            _busy[i]  = false;
            _gen[i]   = 0;
            _since[i] = 0;
        }
    }

    void _clampCap() {
        if (_cfg.maxInFlight > kSlots) _cfg.maxInFlight = kSlots;
    }

    // millis() wraps every ~49.7 days; unsigned subtraction stays correct.
    void _expire(uint32_t nowMs) {
        for (uint8_t i = 0; i < kSlots; i++) {
            if (!_busy[i]) continue;
            if ((uint32_t)(nowMs - _since[i]) < _cfg.slotTtlMs) continue;
            _busy[i] = false;
            if (_inFlight) _inFlight--;
            _slotsExpired++;
        }
    }

    uint16_t _take(uint32_t nowMs) {
        for (uint8_t i = 0; i < kSlots; i++) {
            if (_busy[i]) continue;
            _busy[i]  = true;
            _gen[i]++;               // invalidates any slot still in flight for this slot
            _since[i] = nowMs;
            _inFlight++;
            if (_inFlight > _peakInFlight) _peakInFlight = _inFlight;
            _admittedTotal++;
            return (uint16_t)(((uint16_t)_gen[i] << 8) | i);
        }
        return kNoToken;
    }

    WebAdmissionConfig _cfg;
    uint32_t _since[kSlots] = {0};
    bool     _busy[kSlots]  = {false};
    uint8_t  _gen[kSlots]   = {0};
    uint8_t  _inFlight      = 0;
    uint8_t  _peakInFlight  = 0;
    uint32_t _admittedTotal = 0;
    uint32_t _releasedTotal = 0;
    uint32_t _rejectsConcurrency = 0;
    uint32_t _rejectsReserve     = 0;
    uint32_t _slotsExpired       = 0;
};

#endif  // WEB_ADMISSION_POLICY_H
