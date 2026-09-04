#ifndef TLS_MEMORY_POLICY_H
#define TLS_MEMORY_POLICY_H

#include <stdint.h>

// Optional TLS traffic must leave RAM for AsyncTCP, OTA and the radar task.
//
// dev12: callers now pass byte-addressable-heap figures (MALLOC_CAP_8BIT).
// The previous 48000/24000 were compared against MALLOC_CAP_INTERNAL readings
// that ran ~42 kB high on these boards, so they admitted a handshake whenever
// the IRAM-only region was free — i.e. always — regardless of real headroom.
// Re-expressed against the real heap: an mbedTLS handshake with default IDF
// buffer sizes peaks around 25-30 kB, so require 36 kB free with a 14 kB
// contiguous block. On a board whose usable heap tops out near 45 kB that is
// a genuine admission test rather than a formality.
static constexpr uint32_t TLS_MIN_FREE_HEAP_BYTES = 36000;
static constexpr uint32_t TLS_MIN_LARGEST_BLOCK_BYTES = 14000;

inline bool tlsMemoryAllowsHandshake(uint32_t freeHeap, uint32_t largestBlock,
                                     uint32_t caBytes = 0) {
    return freeHeap >= TLS_MIN_FREE_HEAP_BYTES + caBytes &&
           largestBlock >= TLS_MIN_LARGEST_BLOCK_BYTES + caBytes;
}

#endif
