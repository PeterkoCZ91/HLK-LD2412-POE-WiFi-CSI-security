#ifndef ICSI_MODEL_STORE_H
#define ICSI_MODEL_STORE_H

// Persistence abstraction for the three model slots. Device impl is NVS-backed
// (NvsCsiModelStore); tests use FakeCsiModelStore with failure injection.
// writeSlot() must write→read-back→verify CRC and return false on any failure,
// leaving the previous stored content untouched.

#include "services/CsiSiteModel.h"

struct ICsiModelStore {
    virtual ~ICsiModelStore() {}
    virtual bool writeSlot(CsiModelSlot slot, const CsiSiteModel& m) = 0;
    virtual bool readSlot(CsiModelSlot slot, CsiSiteModel& out) = 0;
    virtual bool eraseSlot(CsiModelSlot slot) = 0;
    virtual uint32_t lastGeneration() = 0;
    virtual void setLastGeneration(uint32_t g) = 0;
};

#endif // ICSI_MODEL_STORE_H
