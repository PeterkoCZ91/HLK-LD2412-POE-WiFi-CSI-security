#ifndef TLS_MEMORY_POLICY_H
#define TLS_MEMORY_POLICY_H

#include <stdint.h>

// Optional TLS traffic must leave RAM for AsyncTCP, OTA and the radar task.
static constexpr uint32_t TLS_MIN_FREE_HEAP_BYTES = 48000;
static constexpr uint32_t TLS_MIN_LARGEST_BLOCK_BYTES = 24000;

inline bool tlsMemoryAllowsHandshake(uint32_t freeHeap, uint32_t largestBlock,
                                     uint32_t caBytes = 0) {
    return freeHeap >= TLS_MIN_FREE_HEAP_BYTES + caBytes &&
           largestBlock >= TLS_MIN_LARGEST_BLOCK_BYTES + caBytes;
}

#endif
