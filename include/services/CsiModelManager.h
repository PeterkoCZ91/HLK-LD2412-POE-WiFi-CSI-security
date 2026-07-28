#ifndef CSI_MODEL_MANAGER_H
#define CSI_MODEL_MANAGER_H

// Pure state machine over three model slots (active/candidate/previous).
// Owns transitions + validation; delegates persistence to ICsiModelStore.
// Knows nothing about WiFi/MQTT/millis — CSIService applies runtime effects
// (threshold/idle baseline) only after a transition reports OK.

#include "services/ICsiModelStore.h"

enum class CsiModelOp { OK, NO_CANDIDATE, NO_PREVIOUS, VALIDATION_FAILED, STORE_FAILED, INCOMPATIBLE_AP };

// Legacy single-model NVS layout (csi_lrn_*) migrated into an active slot once.
struct CsiLegacyModel {
    bool ok = false;
    float thr = 0, mu = 0, std = 0, max = 0, turb = 0, ph = 0, amp = 0;
    uint32_t n = 0;
};

class CsiModelManager {
public:
    explicit CsiModelManager(ICsiModelStore& store) : _store(store) {}

    void loadFromStore() {
        if (!_store.readSlot(CsiModelSlot::ACTIVE, _active))    _active.valid = false;
        if (!_store.readSlot(CsiModelSlot::CANDIDATE, _cand))   _cand.valid = false;
        if (!_store.readSlot(CsiModelSlot::PREVIOUS, _prev))    _prev.valid = false;
    }

    const CsiSiteModel& active()    const { return _active; }
    const CsiSiteModel& candidate() const { return _cand; }
    const CsiSiteModel& previous()  const { return _prev; }
    bool hasCandidate() const { return _cand.valid; }
    bool hasPrevious()  const { return _prev.valid; }

    uint32_t nextGeneration() {
        uint32_t g = _store.lastGeneration() + 1;
        _store.setLastGeneration(g);
        return g;
    }

    CsiModelOp finalizeCandidate(CsiSiteModel cand) {
        cand.valid = true;
        cand.schemaVersion = CSI_MODEL_SCHEMA;
        csiModelSeal(cand);
        if (csiModelValidate(cand) != CsiModelValidity::OK) return CsiModelOp::VALIDATION_FAILED;
        if (!_store.writeSlot(CsiModelSlot::CANDIDATE, cand)) return CsiModelOp::STORE_FAILED;
        _cand = cand;
        return CsiModelOp::OK;
    }

    CsiModelOp applyCandidate() {
        if (!_cand.valid) return CsiModelOp::NO_CANDIDATE;
        if (csiModelValidate(_cand) != CsiModelValidity::OK) return CsiModelOp::VALIDATION_FAILED;

        CsiSiteModel newPrev = _active;   // exact copy incl. EMA edits
        if (_active.valid) {
            newPrev.valid = true; csiModelSeal(newPrev);
            if (!_store.writeSlot(CsiModelSlot::PREVIOUS, newPrev)) return CsiModelOp::STORE_FAILED;
        }
        CsiSiteModel newActive = _cand;
        newActive.valid = true; csiModelSeal(newActive);
        if (!_store.writeSlot(CsiModelSlot::ACTIVE, newActive)) return CsiModelOp::STORE_FAILED;

        // commit in-RAM only after both stores verified
        if (_active.valid) _prev = newPrev;
        _active = newActive;
        return CsiModelOp::OK;
    }

    CsiModelOp rollback() {
        if (!_prev.valid) return CsiModelOp::NO_PREVIOUS;
        CsiSiteModel newActive = _prev;   newActive.valid = true; csiModelSeal(newActive);
        CsiSiteModel newPrev   = _active; newPrev.valid = true;   csiModelSeal(newPrev);
        // Write ACTIVE last (like applyCandidate): if the PREVIOUS write fails the
        // stored active is untouched, so a reboot after a failed rollback never
        // surfaces a half-swapped active. (M-3: writing ACTIVE first left the NVS
        // active rolled back even when rollback() reported STORE_FAILED.)
        if (!_store.writeSlot(CsiModelSlot::PREVIOUS, newPrev))   return CsiModelOp::STORE_FAILED;
        if (!_store.writeSlot(CsiModelSlot::ACTIVE, newActive))   return CsiModelOp::STORE_FAILED;
        _active = newActive; _prev = newPrev;
        return CsiModelOp::OK;
    }

    CsiModelOp clearCandidate() {
        if (!_cand.valid) return CsiModelOp::NO_CANDIDATE;
        // BA-10: only drop the RAM copy if the NVS erase actually succeeded —
        // same guard factoryClear() has. Otherwise RAM says "no candidate"
        // while the slot survives in NVS and resurrects on loadFromStore().
        if (!_store.eraseSlot(CsiModelSlot::CANDIDATE)) return CsiModelOp::STORE_FAILED;
        _cand.valid = false;
        return CsiModelOp::OK;
    }

    // Factory-clear all three slots (active/candidate/previous). Wipes previous
    // too so a later rollback() cannot resurrect a cleared model. Generation
    // counter is left untouched (stays monotonic). CSIService applies the runtime
    // fallback (configured threshold) after this returns.
    //
    // A slot's RAM valid flag is cleared ONLY if its NVS erase succeeded — so if
    // the erase fails, RAM keeps saying the model is present (matching NVS) rather
    // than lying about a clear that a reboot would undo. Returns STORE_FAILED if
    // any erase failed (the store treats an already-absent key as success, so a
    // clean idempotent clear still returns OK).
    CsiModelOp factoryClear() {
        bool ok = true;
        if (_store.eraseSlot(CsiModelSlot::ACTIVE))    _active.valid = false; else ok = false;
        if (_store.eraseSlot(CsiModelSlot::CANDIDATE)) _cand.valid   = false; else ok = false;
        if (_store.eraseSlot(CsiModelSlot::PREVIOUS))  _prev.valid   = false; else ok = false;
        return ok ? CsiModelOp::OK : CsiModelOp::STORE_FAILED;
    }

    // EMA continuous refresh touches ONLY the active slot (best-effort persist).
    CsiModelOp updateActiveEma(float thr, float meanVar, float stdVar) {
        if (!_active.valid) return CsiModelOp::NO_CANDIDATE;
        _active.threshold = thr;
        _active.meanVariance = meanVar;
        _active.stdVariance = stdVar;
        csiModelSeal(_active);
        if (!_store.writeSlot(CsiModelSlot::ACTIVE, _active)) return CsiModelOp::STORE_FAILED;
        return CsiModelOp::OK;
    }

    CsiModelOp migrateLegacy(const CsiLegacyModel& lg) {
        if (_active.valid) return CsiModelOp::OK;         // already migrated
        if (!lg.ok) return CsiModelOp::NO_CANDIDATE;      // nothing to migrate
        CsiSiteModel m;
        m.valid = true; m.schemaVersion = CSI_MODEL_SCHEMA;
        m.generation = nextGeneration();
        m.threshold = lg.thr; m.meanVariance = lg.mu; m.stdVariance = lg.std;
        m.maxVariance = lg.max < lg.mu ? lg.mu : lg.max; // enforce max>=mean invariant
        m.sampleCount = lg.n;
        m.idleMeanTurbulence = lg.turb; m.idleMeanPhase = lg.ph; m.idleAmplitudeBaseline = lg.amp;
        csiModelSeal(m);
        if (csiModelValidate(m) != CsiModelValidity::OK) return CsiModelOp::VALIDATION_FAILED;
        if (!_store.writeSlot(CsiModelSlot::ACTIVE, m)) return CsiModelOp::STORE_FAILED;
        _active = m;
        return CsiModelOp::OK;
    }

protected:
    ICsiModelStore& _store;
    CsiSiteModel _active, _cand, _prev;
};

#endif // CSI_MODEL_MANAGER_H
