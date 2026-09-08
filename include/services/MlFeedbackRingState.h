#ifndef ML_FEEDBACK_RING_STATE_H
#define ML_FEEDBACK_RING_STATE_H

#include <stdint.h>

// Pure ring bookkeeping for MlFeedbackStore (IMPROVEMENTS T9) — capacity,
// head, count and a monotonic sequence counter, no disk I/O. Physical slot
// index and sequence number are assigned in lockstep at write time (same
// insertion order), so paginating "everything after seq N" needs no stored
// seq field on disk: the seq of the i-th oldest surviving record is always
// derivable from (nextSeq - count) + i. Header-only, Arduino-free — native
// tested in test/test_ml_feedback_store.
class MlFeedbackRingState {
public:
    explicit MlFeedbackRingState(uint32_t capacity) : _capacity(capacity) {}

    uint32_t capacity() const { return _capacity; }
    uint32_t count() const { return _count; }
    uint32_t head() const { return _head; }
    uint32_t nextSeq() const { return _nextSeq; }
    uint32_t lastSeq() const { return _nextSeq - 1; }  // 0 before the first write

    // Record a write; returns the physical slot index to write the payload
    // to and assigns it the next sequence number via seqOut.
    uint32_t recordWrite(uint32_t& seqOut) {
        bool willWrap = (_count >= _capacity);
        uint32_t physIdx = willWrap ? _head : (_head + _count) % _capacity;
        seqOut = _nextSeq++;
        if (willWrap) _head = (_head + 1) % _capacity;
        else _count++;
        return physIdx;
    }

    // Physical slot index of the i-th oldest surviving record (0 = oldest).
    uint32_t physIndexOf(uint32_t logicalIndex) const {
        return (_head + logicalIndex) % _capacity;
    }

    // Sequence number of the i-th oldest surviving record.
    uint32_t seqOf(uint32_t logicalIndex) const {
        return (_nextSeq - _count) + logicalIndex;
    }

    // Which logical positions (oldest-first) have seq > afterSeq, capped at
    // `limit`/`outCap`. Returns the count written into outLogicalIdx[].
    uint32_t queryAfter(uint32_t afterSeq, uint32_t limit,
                         uint32_t* outLogicalIdx, uint32_t outCap) const {
        uint32_t written = 0;
        for (uint32_t i = 0; i < _count; i++) {
            if (seqOf(i) <= afterSeq) continue;
            if (written >= limit || written >= outCap) break;
            outLogicalIdx[written++] = i;
        }
        return written;
    }

    void reset() { _head = 0; _count = 0; _nextSeq = 1; }

    // Restore from a disk header on boot. Clamps count to capacity in case
    // the persisted capacity was ever larger (schema shrink).
    void restore(uint32_t head, uint32_t count, uint32_t nextSeq) {
        _head = head;
        _count = (count > _capacity) ? _capacity : count;
        _nextSeq = nextSeq;
    }

private:
    uint32_t _capacity;
    uint32_t _head = 0;
    uint32_t _count = 0;
    uint32_t _nextSeq = 1;  // first write gets seq 1, matches CsiEventRing convention
};

#endif  // ML_FEEDBACK_RING_STATE_H
