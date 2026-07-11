#ifndef CSI_SITE_MODEL_H
#define CSI_SITE_MODEL_H

// Pure data model + CRC + validation for CSI site-learning model slots.
// MUST stay Arduino-free (only <cstdint>/<cstring>/<cmath>) so the model
// state machine can be host-tested via `pio test -e native`, mirroring AlarmFSM.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>

constexpr uint32_t CSI_MODEL_SCHEMA = 1;
constexpr float    CSI_MODEL_MIN_THRESHOLD = 0.005f;  // absolute floor (== CSIService MIN_LEARNED_THRESHOLD)

enum class CsiModelSlot { ACTIVE, CANDIDATE, PREVIOUS };

// Why a learned threshold ended up where it did — surfaced in the quality report.
enum class CsiClampReason : uint8_t {
    NONE = 0, ABSOLUTE_FLOOR = 1, CONFIGURED_FLOOR = 2,
    MAXIMUM_LIMIT = 3, INSUFFICIENT_SAMPLES = 4
};

struct CsiSiteModel {
    bool     valid = false;
    uint32_t schemaVersion = CSI_MODEL_SCHEMA;
    uint32_t generation = 0;
    uint32_t createdAt = 0;
    uint32_t sampleCount = 0;
    uint32_t rejectedMotion = 0;
    uint32_t rejectedRadar = 0;
    uint32_t bssidResetCount = 0;
    uint32_t durationSec = 0;
    float    threshold = 0.0f;
    float    meanVariance = 0.0f;
    float    stdVariance = 0.0f;
    float    maxVariance = 0.0f;
    float    idleMeanTurbulence = 0.0f;
    float    idleMeanPhase = 0.0f;
    float    idleAmplitudeBaseline = 0.0f;
    uint8_t  bssid[6] = {0,0,0,0,0,0};
    uint8_t  _pad[2] = {0,0};
    uint32_t checksum = 0;
};

// Portable CRC32 (IEEE 802.3, reflected) — deterministic on host & device.
inline uint32_t csiModelCrc32(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

// CRC over the whole struct except the trailing checksum field.
inline uint32_t csiModelChecksum(const CsiSiteModel& m) {
    return csiModelCrc32(&m, offsetof(CsiSiteModel, checksum));
}
inline void csiModelSeal(CsiSiteModel& m) { m.checksum = csiModelChecksum(m); }
inline bool csiModelChecksumValid(const CsiSiteModel& m) {
    return m.checksum == csiModelChecksum(m);
}

// --- Validation -------------------------------------------------------------

enum class CsiModelValidity {
    OK, BAD_CHECKSUM, BAD_SCHEMA, NAN_INF,
    THRESHOLD_RANGE, NEGATIVE_STATS, MAX_LT_MEAN, TOO_FEW_SAMPLES
};

inline bool csiFinite(float v) { return !(std::isnan(v) || std::isinf(v)); }

inline CsiModelValidity csiModelValidate(const CsiSiteModel& m, uint32_t minSamples = 30) {
    if (!csiModelChecksumValid(m))                    return CsiModelValidity::BAD_CHECKSUM;
    if (m.schemaVersion != CSI_MODEL_SCHEMA)          return CsiModelValidity::BAD_SCHEMA;
    if (!csiFinite(m.threshold) || !csiFinite(m.meanVariance) ||
        !csiFinite(m.stdVariance) || !csiFinite(m.maxVariance))
                                                      return CsiModelValidity::NAN_INF;
    if (m.threshold < CSI_MODEL_MIN_THRESHOLD || m.threshold > 100.0f)
                                                      return CsiModelValidity::THRESHOLD_RANGE;
    if (m.meanVariance < 0.0f || m.stdVariance < 0.0f || m.maxVariance < 0.0f)
                                                      return CsiModelValidity::NEGATIVE_STATS;
    if (m.maxVariance < m.meanVariance)               return CsiModelValidity::MAX_LT_MEAN;
    if (m.sampleCount < minSamples)                   return CsiModelValidity::TOO_FEW_SAMPLES;
    return CsiModelValidity::OK;
}

// --- Quality report ---------------------------------------------------------

struct CsiModelQuality {
    uint32_t schemaVersion = CSI_MODEL_SCHEMA;
    uint32_t generation = 0, accepted = 0, rejectedMotion = 0, rejectedRadar = 0;
    float    p50 = 0, p90 = 0, p95 = 0, p99 = 0, mean = 0, std = 0, max = 0;
    uint8_t  thresholdClampReason = 0;   // CsiClampReason
    uint8_t  _pad[3] = {0,0,0};
    uint32_t checksum = 0;
};
inline uint32_t csiQualityChecksum(const CsiModelQuality& q) {
    return csiModelCrc32(&q, offsetof(CsiModelQuality, checksum));
}
inline void csiQualitySeal(CsiModelQuality& q) { q.checksum = csiQualityChecksum(q); }
inline bool csiQualityChecksumValid(const CsiModelQuality& q) {
    return q.checksum == csiQualityChecksum(q);
}

// Log-bucketed variance histogram accumulated during learning → quality quantiles.
class CsiVarianceHistogram {
public:
    static constexpr int NB = 64;
    void reset() { _total = 0; for (int i = 0; i < NB; i++) _b[i] = 0; }
    void add(float v) {
        _total++;
        int idx = _bucket(v);
        if (_b[idx] < 0xFFFF) _b[idx]++;
    }
    uint32_t total() const { return _total; }
    float quantile(float q) const {                // q in [0,1]
        if (_total == 0) return 0.0f;
        uint32_t target = (uint32_t)(q * _total);
        uint32_t cum = 0;
        for (int i = 0; i < NB; i++) { cum += _b[i]; if (cum >= target) return _edgeHi(i); }
        return _edgeHi(NB - 1);
    }
private:
    static constexpr float LO = CSI_MODEL_MIN_THRESHOLD / 4.0f;  // 0.00125
    static constexpr float HI = 0.5f;
    static int _bucket(float v) {
        if (v <= LO) return 0;
        if (v >= HI) return NB - 1;
        float lr = std::log(v / LO) / std::log(HI / LO);
        int idx = (int)(lr * (NB - 1));
        if (idx < 0) idx = 0;
        if (idx > NB - 1) idx = NB - 1;
        return idx;
    }
    static float _edgeHi(int i) {
        float lr = (float)(i + 1) / (float)(NB - 1);
        return LO * std::exp(lr * std::log(HI / LO));
    }
    uint16_t _b[NB] = {0};
    uint32_t _total = 0;
};

#endif // CSI_SITE_MODEL_H
