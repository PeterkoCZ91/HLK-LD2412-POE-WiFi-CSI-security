#ifndef CSI_MODEL_COMMAND_H
#define CSI_MODEL_COMMAND_H

#include <atomic>
#include <stdint.h>
#include "services/CsiModelManager.h"

enum class CsiModelCommand : uint8_t {
    APPLY,
    ROLLBACK,
    CLEAR_CANDIDATE,
    CLEAR_ALL,
    IMPORT   // land a web-imported model as candidate — routed here so the _cand
             // write is serialized on csi_proc with APPLY (no async_tcp race)
};

enum class CsiModelRequestStatus : uint8_t {
    COMPLETED,
    BUSY,
    TIMED_OUT
};

enum class CsiModelCommandState : uint8_t {
    IDLE,
    CLAIMED,
    PENDING,
    EXECUTING,
    DONE
};

// One bounded web->worker command slot. Only one destructive model transition
// may be in flight; a timed-out waiter either cancels a pending command or marks
// an executing command abandoned so the worker releases the slot on completion.
class CsiModelCommandSlot {
public:
    bool submit(CsiModelCommand command, bool force) {
        CsiModelCommandState expected = CsiModelCommandState::IDLE;
        if (!_state.compare_exchange_strong(expected, CsiModelCommandState::CLAIMED,
                                            std::memory_order_acq_rel)) {
            return false;
        }
        _command = command;
        _force = force;
        _abandoned.store(false, std::memory_order_relaxed);
        _state.store(CsiModelCommandState::PENDING, std::memory_order_release);
        return true;
    }

    // IMPORT variant: carries the model payload. _payload is written before the
    // release store on _state, so the claimer's acquire sees a consistent copy.
    bool submit(CsiModelCommand command, bool force, const CsiSiteModel& payload) {
        CsiModelCommandState expected = CsiModelCommandState::IDLE;
        if (!_state.compare_exchange_strong(expected, CsiModelCommandState::CLAIMED,
                                            std::memory_order_acq_rel)) {
            return false;
        }
        _command = command;
        _force = force;
        _payload = payload;
        _abandoned.store(false, std::memory_order_relaxed);
        _state.store(CsiModelCommandState::PENDING, std::memory_order_release);
        return true;
    }

    // Valid only for the worker between claim() and complete().
    const CsiSiteModel& payload() const { return _payload; }

    bool claim(CsiModelCommand& command, bool& force) {
        CsiModelCommandState expected = CsiModelCommandState::PENDING;
        if (!_state.compare_exchange_strong(expected, CsiModelCommandState::EXECUTING,
                                            std::memory_order_acq_rel)) {
            return false;
        }
        command = _command;
        force = _force;
        return true;
    }

    void complete(CsiModelOp result) {
        _result = result;
        if (_abandoned.exchange(false, std::memory_order_acq_rel)) {
            _state.store(CsiModelCommandState::IDLE, std::memory_order_release);
        } else {
            _state.store(CsiModelCommandState::DONE, std::memory_order_release);
        }
    }

    bool poll(CsiModelOp& result) {
        CsiModelCommandState expected = CsiModelCommandState::DONE;
        if (!_state.compare_exchange_strong(expected, CsiModelCommandState::CLAIMED,
                                            std::memory_order_acq_rel)) {
            return false;
        }
        result = _result;
        _abandoned.store(false, std::memory_order_relaxed);
        _state.store(CsiModelCommandState::IDLE, std::memory_order_release);
        return true;
    }

    void abandon() {
        _abandoned.store(true, std::memory_order_release);
        CsiModelCommandState expected = CsiModelCommandState::PENDING;
        if (_state.compare_exchange_strong(expected, CsiModelCommandState::IDLE,
                                           std::memory_order_acq_rel)) {
            _abandoned.store(false, std::memory_order_relaxed);
        }
    }

    CsiModelCommandState state() const {
        return _state.load(std::memory_order_acquire);
    }

private:
    std::atomic<CsiModelCommandState> _state{CsiModelCommandState::IDLE};
    std::atomic<bool> _abandoned{false};
    CsiModelCommand _command = CsiModelCommand::APPLY;
    bool _force = false;
    CsiModelOp _result = CsiModelOp::STORE_FAILED;
    CsiSiteModel _payload;   // IMPORT payload; written under the state release/acquire fence
};

#endif
