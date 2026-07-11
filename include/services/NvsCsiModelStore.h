#ifndef NVS_CSI_MODEL_STORE_H
#define NVS_CSI_MODEL_STORE_H

// Device-side ICsiModelStore backed by Arduino Preferences (NVS). Each slot is a
// single versioned blob; writeSlot() does write -> read-back -> CRC verify so a
// reboot mid-write can never surface a half-written model as valid.
// NOTE: includes Preferences.h -> NOT host-testable; the logic it wraps is
// covered by the pure CsiModelManager tests via FakeCsiModelStore.

#include <Preferences.h>
#include "services/ICsiModelStore.h"

class NvsCsiModelStore : public ICsiModelStore {
public:
    NvsCsiModelStore() {}
    void attach(Preferences* prefs) { _prefs = prefs; }

    bool writeSlot(CsiModelSlot slot, const CsiSiteModel& m) override;
    bool readSlot(CsiModelSlot slot, CsiSiteModel& out) override;
    bool eraseSlot(CsiModelSlot slot) override;
    uint32_t lastGeneration() override;
    void setLastGeneration(uint32_t g) override;

private:
    static const char* _slotKey(CsiModelSlot slot);
    Preferences* _prefs = nullptr;
};

#endif // NVS_CSI_MODEL_STORE_H
