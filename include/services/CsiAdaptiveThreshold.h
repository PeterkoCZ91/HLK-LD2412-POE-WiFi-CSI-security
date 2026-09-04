#ifndef CSI_ADAPTIVE_THRESHOLD_H
#define CSI_ADAPTIVE_THRESHOLD_H

#include <algorithm>
#include <cstdint>
#include <cstdlib>

// #13: runtime adaptive detection threshold = the q-quantile of the rolling
// idle-variance buffer, scaled by a margin factor. P95 (q=0.95) is the sensitive
// default; P99 (q=0.99) rides higher on the noise tail, cutting false positives
// on noisy links at the cost of sensitivity. `scratch` (length >= n) is a
// caller-owned working copy of the ring buffer — nth_element reorders it in place.
//
// Pure/Arduino-free so it is host-tested natively (see test_csi_adaptive_threshold).

// Clamp a requested percentile to the supported [0.50, 0.999] band. Values
// outside snap to the nearest edge; NaN falls back to the P95 default.
inline float csiClampAdaptivePercentile(float q) {
    if (!(q == q)) return 0.95f;          // NaN guard
    if (q < 0.50f) return 0.50f;
    if (q > 0.999f) return 0.999f;
    return q;
}

// Strict parse of an API-supplied percentile: a fraction ("0.95"/"0.99") or a
// whole percent ("95"/"99.9"). Returns false — leaving *out untouched — for
// null/empty/unparseable input, trailing junk, or values outside [0.50, 0.999].
// Invalid input must be rejected, never coerced: Arduino toFloat() returns 0
// for garbage, which the clamp would turn into a persisted P50.
inline bool csiParseAdaptivePercentile(const char* s, float* out) {
    if (s == nullptr || *s == '\0' || out == nullptr) return false;
    char* end = nullptr;
    double v = strtod(s, &end);
    if (end == s || *end != '\0') return false;
    if (v > 1.0) v /= 100.0;              // 95 -> 0.95
    const float f = (float)v;             // compare in float so 99.9/100 == 0.999f
    if (!(f >= 0.50f && f <= 0.999f)) return false;  // NaN/inf fail here too
    *out = f;
    return true;
}

// Nearest-rank 0-based index into an ascending order of n samples for quantile q.
inline uint16_t csiPercentileIndex(uint16_t n, float q) {
    if (n == 0) return 0;
    if (q < 0.0f) q = 0.0f;
    if (q > 1.0f) q = 1.0f;
    uint32_t idx = (uint32_t)((n - 1) * q);
    return idx < n ? (uint16_t)idx : (uint16_t)(n - 1);
}

// Scaled q-quantile of scratch[0..n). Returns 0 when there is no data. Reorders
// scratch in place (partial sort around the quantile index).
inline float csiAdaptiveThreshold(float* scratch, uint16_t n, float q, float factor) {
    if (scratch == nullptr || n == 0) return 0.0f;
    uint16_t idx = csiPercentileIndex(n, q);
    std::nth_element(scratch, scratch + idx, scratch + n);
    return scratch[idx] * factor;
}

#endif
