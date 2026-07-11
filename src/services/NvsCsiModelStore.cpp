#include "services/NvsCsiModelStore.h"

const char* NvsCsiModelStore::_slotKey(CsiModelSlot slot) {
    switch (slot) {
        case CsiModelSlot::ACTIVE:    return "csi_model_act";
        case CsiModelSlot::CANDIDATE: return "csi_model_cand";
        case CsiModelSlot::PREVIOUS:  return "csi_model_prev";
    }
    return "csi_model_x";
}

bool NvsCsiModelStore::writeSlot(CsiModelSlot slot, const CsiSiteModel& m) {
    if (!_prefs) return false;
    CsiSiteModel w = m;
    csiModelSeal(w);
    size_t n = _prefs->putBytes(_slotKey(slot), &w, sizeof(w));
    if (n != sizeof(w)) return false;
    // read-back verify: a valid slot is only committed once its CRC survives NVS
    CsiSiteModel rb;
    if (_prefs->getBytes(_slotKey(slot), &rb, sizeof(rb)) != sizeof(rb)) return false;
    return csiModelChecksumValid(rb) && rb.generation == w.generation;
}

bool NvsCsiModelStore::readSlot(CsiModelSlot slot, CsiSiteModel& out) {
    if (!_prefs) return false;
    // Guard with isKey() first: getBytes() on a missing blob emits a noisy
    // ESP_LOGE on every boot until a model exists. A never-learned node stays quiet.
    if (!_prefs->isKey(_slotKey(slot))) return false;
    CsiSiteModel rb;
    if (_prefs->getBytes(_slotKey(slot), &rb, sizeof(rb)) != sizeof(rb)) return false;
    if (!csiModelChecksumValid(rb)) return false;
    if (rb.schemaVersion != CSI_MODEL_SCHEMA) return false;
    out = rb;
    return true;
}

bool NvsCsiModelStore::eraseSlot(CsiModelSlot slot) {
    return _prefs ? _prefs->remove(_slotKey(slot)) : false;
}

uint32_t NvsCsiModelStore::lastGeneration() {
    return _prefs ? _prefs->getUInt("csi_model_gen", 0) : 0;
}

void NvsCsiModelStore::setLastGeneration(uint32_t g) {
    if (_prefs) _prefs->putUInt("csi_model_gen", g);
}
