#ifndef ML_LAST_INFERENCE_H
#define ML_LAST_INFERENCE_H

#include "services/ml_features.h"
#include <atomic>

static constexpr uint32_t ML_FEEDBACK_MAX_AGE_MS = 5000;

// Last-computed ML feature vector (IMPROVEMENTS T9) — single-slot stash,
// same lifetime/staleness pattern as CsiDecisionTrace: overwritten every
// time CSIService::_runMlInference() actually computes features, `valid`
// stays false until the first one. Lets a UI "false alarm / confirm motion"
// button capture the EXACT vector that produced the last ML decision,
// instead of recomputing from a turbulence buffer that may have moved on
// by the time the user clicks.
struct MlLastInference {
    bool     valid = false;
    float    feats[csi_ml::ML_NUM_FEATURES] = {0};
    uint32_t uptimeMs = 0;
};

// Single CSI writer; atomic payload prevents races with async web readers.
class MlLastInferenceStore {
public:
    void publish(const MlLastInference& value) {
        _seq.fetch_add(1);
        for (unsigned i = 0; i < csi_ml::ML_NUM_FEATURES; ++i)
            _feats[i].store(value.feats[i]);
        _uptimeMs.store(value.uptimeMs);
        _valid.store(value.valid);
        _seq.fetch_add(1);
    }

    bool read(MlLastInference& out) const {
        for (unsigned attempt = 0; attempt < 16; ++attempt) {
            const uint32_t before = _seq.load();
            if (before & 1U) continue;
            MlLastInference candidate;
            for (unsigned i = 0; i < csi_ml::ML_NUM_FEATURES; ++i)
                candidate.feats[i] = _feats[i].load();
            candidate.uptimeMs = _uptimeMs.load();
            candidate.valid = _valid.load();
            if (before == _seq.load()) { out = candidate; return true; }
        }
        return false;
    }

private:
    std::atomic<uint32_t> _seq{0};
    std::atomic<float> _feats[csi_ml::ML_NUM_FEATURES]{};
    std::atomic<uint32_t> _uptimeMs{0};
    std::atomic<bool> _valid{false};
};

#endif  // ML_LAST_INFERENCE_H
