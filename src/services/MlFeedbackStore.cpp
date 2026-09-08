#include "services/MlFeedbackStore.h"
#include "debug.h"
#include <cmath>
#include "services/MlFeedbackCommit.h"

void MlFeedbackStore::begin(bool fsAvailable) {
    _fsAvailable = fsAvailable;
    if (_fsAvailable) loadFromDisk();
    DBG("MlFeedback", "Ready — %u/%u samples, fs=%d", _ring.count(), ML_FEEDBACK_CAPACITY, _fsAvailable);
}

bool MlFeedbackStore::addSample(const float feats[csi_ml::ML_NUM_FEATURES], uint8_t label) {
    if (!_fsAvailable || !feats || label > 1 || _ring.nextSeq() == UINT32_MAX) return false;
    for (uint8_t i = 0; i < csi_ml::ML_NUM_FEATURES; ++i)
        if (!std::isfinite(feats[i])) return false;

    MlLabeledSample sample{};
    memcpy(sample.feats, feats, sizeof(sample.feats));
    sample.ts = millis() / 1000;
    sample.label = label;

    // Copy-on-write: the old file remains authoritative until LittleFS rename
    // commits the complete replacement. In particular, a full ring must not
    // overwrite its oldest sample before the new header is durable.
    MlFeedbackRingState next = _ring;
    uint32_t seq;
    uint32_t slot = next.recordWrite(seq);
    MlFeedbackFileHeader hdr{};
    hdr.magic = ML_FEEDBACK_FILE_MAGIC;
    hdr.capacity = ML_FEEDBACK_CAPACITY;
    hdr.head = next.head();
    hdr.count = next.count();
    hdr.nextSeq = next.nextSeq();
    if (!commitFeedbackFile(LittleFS, _filename, "/ml_feedback.tmp", hdr, sample,
                            ML_FEEDBACK_CAPACITY, slot, _ring.count() != 0)) return false;
    _ring = next;
    DBG("MlFeedback", "Sample stored: label=%u seq=%u (%u/%u)", label, seq, _ring.count(), ML_FEEDBACK_CAPACITY);
    return true;
}

bool MlFeedbackStore::readSampleAt(uint32_t physIdx, MlLabeledSample& out) {
    File f = LittleFS.open(_filename, "r");
    if (!f) return false;
    size_t offset = ML_FEEDBACK_HEADER_SIZE + (size_t)physIdx * ML_FEEDBACK_SAMPLE_SIZE;
    if (!f.seek(offset)) { f.close(); return false; }
    bool ok = f.read((uint8_t*)&out, ML_FEEDBACK_SAMPLE_SIZE) == ML_FEEDBACK_SAMPLE_SIZE;
    f.close();
    if (!ok || out.label > 1) return false;
    for (uint8_t i = 0; i < csi_ml::ML_NUM_FEATURES; ++i)
        if (!std::isfinite(out.feats[i])) return false;
    return true;
}

void MlFeedbackStore::loadFromDisk() {
    if (!LittleFS.exists(_filename)) return;
    File f = LittleFS.open(_filename, "r");
    MlFeedbackFileHeader hdr{};
    bool ok = f && f.size() == ML_FEEDBACK_HEADER_SIZE +
        ML_FEEDBACK_CAPACITY * ML_FEEDBACK_SAMPLE_SIZE &&
        f.read((uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr);
    f.close();
    ok = ok && hdr.magic == ML_FEEDBACK_FILE_MAGIC && hdr.capacity == ML_FEEDBACK_CAPACITY &&
        hdr.head < ML_FEEDBACK_CAPACITY && hdr.count <= ML_FEEDBACK_CAPACITY &&
        hdr.nextSeq > hdr.count && (hdr.count == ML_FEEDBACK_CAPACITY || hdr.head == 0);
    if (!ok) {
        // Preserve invalid data for recovery; never silently delete training evidence.
        _fsAvailable = false;
        DBG("MlFeedback", "Invalid feedback file; preserved for recovery");
        return;
    }
    _ring.restore(hdr.head, hdr.count, hdr.nextSeq);
}

void MlFeedbackStore::getFeedbackJSON(JsonDocument& doc, uint32_t afterSeq, uint16_t limit) {
    JsonObject root = doc.to<JsonObject>();
    root["total"] = _ring.count();
    root["capacity"] = ML_FEEDBACK_CAPACITY;
    root["last_seq"] = _ring.lastSeq();
    root["available"] = _fsAvailable;
    root["schema"] = "poe2412.ml-feedback.v1";
    root["feature_count"] = csi_ml::ML_NUM_FEATURES;
    root["max_page_size"] = ML_FEEDBACK_PAGE_SIZE;
    JsonArray arr = root["samples"].to<JsonArray>();
    uint32_t logicalIdx[ML_FEEDBACK_PAGE_SIZE];
    uint16_t cap = std::min<uint16_t>(limit, ML_FEEDBACK_PAGE_SIZE);
    uint32_t n = _fsAvailable ? _ring.queryAfter(afterSeq, cap, logicalIdx, ML_FEEDBACK_PAGE_SIZE) : 0;
    uint32_t returned = 0, nextSeq = afterSeq;
    bool readError = false;
    for (uint32_t i = 0; i < n; i++) {
        MlLabeledSample s{};
        if (!readSampleAt(_ring.physIndexOf(logicalIdx[i]), s)) {
            readError = true;
            break; // Do not skip an unreadable sample and silently advance the cursor.
        }
        JsonObject o = arr.add<JsonObject>();
        nextSeq = _ring.seqOf(logicalIdx[i]);
        o["seq"] = nextSeq;
        o["ts"] = s.ts;
        o["label"] = s.label;
        JsonArray f = o["feats"].to<JsonArray>();
        for (uint8_t k = 0; k < csi_ml::ML_NUM_FEATURES; k++) f.add(s.feats[k]);
        returned++;
    }
    root["returned"] = returned;
    root["next_seq"] = nextSeq;
    root["read_error"] = readError;
}
