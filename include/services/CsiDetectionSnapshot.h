#ifndef CSI_DETECTION_SNAPSHOT_H
#define CSI_DETECTION_SNAPSHOT_H

#include <atomic>
#include <stdint.h>

struct CsiDetectionSnapshot {
    bool motion = false;
    bool mlMotion = false;
    bool mlEnabled = false;
    float compositeScore = 0.0f;
    float breathingScore = 0.0f;
    float mlProbability = 0.0f;
    float mlThreshold = 0.5f;
    bool dataOk = true;
    uint32_t seq = 0;
};

// Single-writer seqlock. Payload fields are atomic too, avoiding C++ data races
// while the sequence check prevents readers from accepting a mixed generation.
class CsiDetectionSnapshotStore {
public:
    void publish(const CsiDetectionSnapshot& value) {
        _seq.fetch_add(1, std::memory_order_seq_cst);  // odd: write in progress
        _motion.store(value.motion, std::memory_order_seq_cst);
        _mlMotion.store(value.mlMotion, std::memory_order_seq_cst);
        _mlEnabled.store(value.mlEnabled, std::memory_order_seq_cst);
        _compositeScore.store(value.compositeScore, std::memory_order_seq_cst);
        _breathingScore.store(value.breathingScore, std::memory_order_seq_cst);
        _mlProbability.store(value.mlProbability, std::memory_order_seq_cst);
        _mlThreshold.store(value.mlThreshold, std::memory_order_seq_cst);
        _dataOk.store(value.dataOk, std::memory_order_seq_cst);
        _seq.fetch_add(1, std::memory_order_seq_cst);  // even: committed
    }

    bool read(CsiDetectionSnapshot& out, uint32_t maxAttempts = 64) const {
        for (uint32_t attempt = 0; attempt < maxAttempts; attempt++) {
            uint32_t before = _seq.load(std::memory_order_acquire);
            if (before & 1U) continue;

            CsiDetectionSnapshot candidate;
            candidate.motion = _motion.load(std::memory_order_relaxed);
            candidate.mlMotion = _mlMotion.load(std::memory_order_relaxed);
            candidate.mlEnabled = _mlEnabled.load(std::memory_order_relaxed);
            candidate.compositeScore = _compositeScore.load(std::memory_order_relaxed);
            candidate.breathingScore = _breathingScore.load(std::memory_order_relaxed);
            candidate.mlProbability = _mlProbability.load(std::memory_order_relaxed);
            candidate.mlThreshold = _mlThreshold.load(std::memory_order_relaxed);
            candidate.dataOk = _dataOk.load(std::memory_order_relaxed);

            uint32_t after = _seq.load(std::memory_order_acquire);
            if (before == after && !(after & 1U)) {
                candidate.seq = after;
                out = candidate;
                return true;
            }
        }
        return false;
    }

private:
    std::atomic<uint32_t> _seq{0};
    std::atomic<bool> _motion{false};
    std::atomic<bool> _mlMotion{false};
    std::atomic<bool> _mlEnabled{false};
    std::atomic<float> _compositeScore{0.0f};
    std::atomic<float> _breathingScore{0.0f};
    std::atomic<float> _mlProbability{0.0f};
    std::atomic<float> _mlThreshold{0.5f};
    std::atomic<bool> _dataOk{true};
};

#endif
