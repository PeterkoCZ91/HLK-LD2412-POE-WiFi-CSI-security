#ifndef ML_FEEDBACK_STORE_H
#define ML_FEEDBACK_STORE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include "services/ml_features.h"
#include "services/MlFeedbackRingState.h"

// On-device ML false-alarm feedback loop (IMPROVEMENTS T9). A UI button
// ("false alarm" / "confirm motion") captures the exact 17-feature vector
// that produced the last ML decision (see MlLastInference) and labels it,
// so the sister project's offline retrain pipeline has real field samples
// instead of only the original espectre training set. Label encoding:
//   0 = no_motion (operator marked this a false alarm)
//   1 = motion     (operator confirmed real motion)
//
// Writes are user-driven (a button press), not a telemetry stream, so —
// unlike EventLog — there is no RAM write-back cache or rate-limited flush:
// addSample() writes straight to disk. Ring bookkeeping (wraparound,
// sequence numbers, pagination) is delegated to MlFeedbackRingState, which
// is pure and natively tested; this class only adds the LittleFS I/O.

struct MlLabeledSample {
    float    feats[csi_ml::ML_NUM_FEATURES];
    uint32_t ts;      // uptime seconds at label time
    uint8_t  label;   // 0 = no_motion, 1 = motion
};

struct MlFeedbackFileHeader {
    uint32_t magic;
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
    uint32_t nextSeq;
};

// Schema version folded into the magic — bump this (not just the header
// layout) whenever ML_NUM_FEATURES or MlLabeledSample changes, so an old
// on-disk file is reinitialized rather than misread as the new shape.
static constexpr uint32_t ML_FEEDBACK_FILE_MAGIC = 0xF7ED0001;
static constexpr uint32_t ML_FEEDBACK_CAPACITY   = 500;
static constexpr uint16_t ML_FEEDBACK_PAGE_SIZE = 8;
static constexpr size_t   ML_FEEDBACK_HEADER_SIZE = sizeof(MlFeedbackFileHeader);
static constexpr size_t   ML_FEEDBACK_SAMPLE_SIZE = sizeof(MlLabeledSample);

class MlFeedbackStore {
public:
    // begin() expects LittleFS already mounted externally (main.cpp).
    void begin(bool fsAvailable = true);

    // Persist one labeled sample. Returns false if the filesystem isn't
    // available or the write failed (ring state is left untouched on
    // failure, matching EventLog::writeEventToDisk's fail-safe ordering).
    bool addSample(const float feats[csi_ml::ML_NUM_FEATURES], uint8_t label);

    // Paginated export, oldest-first after `afterSeq`, capped at `limit`.
    void getFeedbackJSON(JsonDocument& doc, uint32_t afterSeq, uint16_t limit);

    uint32_t count() const { return _ring.count(); }
    uint32_t capacity() const { return ML_FEEDBACK_CAPACITY; }
    uint32_t lastSeq() const { return _ring.lastSeq(); }

private:
    void loadFromDisk();
    bool readSampleAt(uint32_t physIdx, MlLabeledSample& out);

    MlFeedbackRingState _ring{ML_FEEDBACK_CAPACITY};
    bool _fsAvailable = false;
    const char* _filename = "/ml_feedback.bin";
};

#endif  // ML_FEEDBACK_STORE_H
