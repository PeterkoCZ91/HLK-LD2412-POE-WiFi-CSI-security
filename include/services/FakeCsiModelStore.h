#ifndef FAKE_CSI_MODEL_STORE_H
#define FAKE_CSI_MODEL_STORE_H

// In-RAM store for native unit tests. Supports failure injection so apply/rollback
// atomicity can be verified without hardware.

#include "services/ICsiModelStore.h"

class FakeCsiModelStore : public ICsiModelStore {
public:
    bool present[3] = {false, false, false};
    CsiSiteModel slots[3];
    uint32_t gen = 0;

    // failure injection
    int failSlotMask = 0;   // bit per slot -> writeSlot(slot) returns false
    int failNextN = 0;      // fail the next N writes regardless of slot

    static int idx(CsiModelSlot s) { return static_cast<int>(s); }

    bool writeSlot(CsiModelSlot slot, const CsiSiteModel& m) override {
        if (failNextN > 0) { failNextN--; return false; }
        if (failSlotMask & (1 << idx(slot))) return false;
        CsiSiteModel copy = m;
        csiModelSeal(copy);
        if (!csiModelChecksumValid(copy)) return false;   // simulate read-back verify
        slots[idx(slot)] = copy;
        present[idx(slot)] = true;
        return true;
    }
    bool readSlot(CsiModelSlot slot, CsiSiteModel& out) override {
        if (!present[idx(slot)]) return false;
        if (!csiModelChecksumValid(slots[idx(slot)])) return false;
        out = slots[idx(slot)];
        return true;
    }
    bool eraseSlot(CsiModelSlot slot) override {
        present[idx(slot)] = false;
        return true;
    }
    uint32_t lastGeneration() override { return gen; }
    void setLastGeneration(uint32_t g) override { gen = g; }
};

#endif // FAKE_CSI_MODEL_STORE_H
