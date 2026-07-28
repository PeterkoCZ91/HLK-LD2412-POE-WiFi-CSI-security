#pragma once

#include <atomic>
#include <stdint.h>

enum class RuntimeOperation : uint8_t {
    IDLE = 0,
    WIFI_SCAN,
    CALIBRATION,
    SITE_LEARNING,
    OTA,
    RESTART,
};

enum class RuntimeOperationFailure : uint8_t {
    NONE = 0,
    BUSY,
    TIMED_OUT,
    CANCELLED,
};

inline const char* runtimeOperationText(RuntimeOperation operation) {
    switch (operation) {
        case RuntimeOperation::IDLE:          return "idle";
        case RuntimeOperation::WIFI_SCAN:     return "wifi_scan";
        case RuntimeOperation::CALIBRATION:   return "calibration";
        case RuntimeOperation::SITE_LEARNING: return "site_learning";
        case RuntimeOperation::OTA:           return "ota";
        case RuntimeOperation::RESTART:       return "restart";
    }
    return "unknown";
}

inline const char* runtimeOperationFailureText(RuntimeOperationFailure failure) {
    switch (failure) {
        case RuntimeOperationFailure::NONE:      return "none";
        case RuntimeOperationFailure::BUSY:      return "busy";
        case RuntimeOperationFailure::TIMED_OUT: return "timed_out";
        case RuntimeOperationFailure::CANCELLED: return "cancelled";
    }
    return "unknown";
}

struct RuntimeOperationStatus {
    RuntimeOperation operation = RuntimeOperation::IDLE;
    RuntimeOperationFailure failure = RuntimeOperationFailure::NONE;
    uint8_t ownerId = 0;
    uint32_t lastProgressMs = 0;
    uint32_t timeoutMs = 0;
};

// Single-owner, wrap-safe runtime operation lifecycle. This is deliberately
// platform-free so native tests can characterize ownership and timeout rules
// before hardware services are migrated onto it.
class RuntimeOperationCoordinator {
public:
    bool tryBegin(RuntimeOperation operation, uint32_t nowMs, uint32_t timeoutMs,
                  uint8_t ownerId = 0) {
        if (operation == RuntimeOperation::IDLE || timeoutMs == 0) return false;

        uint16_t expected = CLAIM_IDLE;
        if (!_claim.compare_exchange_strong(expected, CLAIM_CHANGING,
                                            std::memory_order_acq_rel)) {
            return false;
        }
        _failure.store(static_cast<uint8_t>(RuntimeOperationFailure::NONE),
                       std::memory_order_relaxed);
        _lastProgressMs.store(nowMs, std::memory_order_relaxed);
        _timeoutMs.store(timeoutMs, std::memory_order_relaxed);
        _claim.store(makeClaim(operation, ownerId), std::memory_order_release);
        return true;
    }

    bool markProgress(RuntimeOperation operation, uint32_t nowMs, uint8_t ownerId = 0) {
        if (operation == RuntimeOperation::IDLE ||
            _claim.load(std::memory_order_acquire) != makeClaim(operation, ownerId)) {
            return false;
        }
        _lastProgressMs.store(nowMs, std::memory_order_release);
        return true;
    }

    bool finish(RuntimeOperation operation, uint8_t ownerId = 0) {
        if (!claimForClear(operation, ownerId)) return false;
        clear(RuntimeOperationFailure::NONE);
        return true;
    }

    bool cancel(RuntimeOperation operation, uint8_t ownerId = 0) {
        if (!claimForClear(operation, ownerId)) return false;
        clear(RuntimeOperationFailure::CANCELLED);
        return true;
    }

    bool checkTimeout(uint32_t nowMs, RuntimeOperationStatus* timedOut = nullptr) {
        return checkTimeout(RuntimeOperation::IDLE, nowMs, timedOut);
    }

    // Restrict a watchdog to its own operation. A subsystem must never time out
    // and release a different operation that happens to own the shared runtime.
    bool checkTimeout(RuntimeOperation expectedOperation, uint32_t nowMs,
                      RuntimeOperationStatus* timedOut = nullptr) {
        uint16_t claim = _claim.load(std::memory_order_acquire);
        if (!isActiveClaim(claim)) return false;
        if (expectedOperation != RuntimeOperation::IDLE &&
            operationFromClaim(claim) != expectedOperation) {
            return false;
        }

        uint32_t lastProgressMs = _lastProgressMs.load(std::memory_order_acquire);
        uint32_t timeoutMs = _timeoutMs.load(std::memory_order_acquire);
        if (static_cast<int32_t>(nowMs - lastProgressMs) <=
            static_cast<int32_t>(timeoutMs)) {
            return false;
        }

        if (!_claim.compare_exchange_strong(claim, CLAIM_CHANGING,
                                            std::memory_order_acq_rel)) {
            return false;
        }
        if (timedOut) {
            timedOut->operation = operationFromClaim(claim);
            timedOut->failure = RuntimeOperationFailure::TIMED_OUT;
            timedOut->ownerId = ownerFromClaim(claim);
            timedOut->lastProgressMs = lastProgressMs;
            timedOut->timeoutMs = timeoutMs;
        }
        clear(RuntimeOperationFailure::TIMED_OUT);
        return true;
    }

    RuntimeOperationStatus status() const {
        RuntimeOperationStatus result;
        uint16_t claim = _claim.load(std::memory_order_acquire);
        result.failure = static_cast<RuntimeOperationFailure>(
            _failure.load(std::memory_order_acquire));
        if (!isActiveClaim(claim)) return result;

        result.operation = operationFromClaim(claim);
        result.ownerId = ownerFromClaim(claim);
        result.lastProgressMs = _lastProgressMs.load(std::memory_order_acquire);
        result.timeoutMs = _timeoutMs.load(std::memory_order_acquire);
        return result;
    }

private:
    static constexpr uint16_t CLAIM_IDLE = 0;
    static constexpr uint16_t CLAIM_CHANGING = 0xFFFF;

    static uint16_t makeClaim(RuntimeOperation operation, uint8_t ownerId) {
        return static_cast<uint16_t>(static_cast<uint8_t>(operation)) |
               (static_cast<uint16_t>(ownerId) << 8);
    }

    static bool isActiveClaim(uint16_t claim) {
        return claim != CLAIM_IDLE && claim != CLAIM_CHANGING;
    }

    static RuntimeOperation operationFromClaim(uint16_t claim) {
        return static_cast<RuntimeOperation>(claim & 0xFF);
    }

    static uint8_t ownerFromClaim(uint16_t claim) {
        return static_cast<uint8_t>(claim >> 8);
    }

    bool claimForClear(RuntimeOperation operation, uint8_t ownerId) {
        if (operation == RuntimeOperation::IDLE) return false;
        uint16_t expected = makeClaim(operation, ownerId);
        return _claim.compare_exchange_strong(expected, CLAIM_CHANGING,
                                              std::memory_order_acq_rel);
    }

    void clear(RuntimeOperationFailure failure) {
        _lastProgressMs.store(0, std::memory_order_relaxed);
        _timeoutMs.store(0, std::memory_order_relaxed);
        _failure.store(static_cast<uint8_t>(failure), std::memory_order_relaxed);
        _claim.store(CLAIM_IDLE, std::memory_order_release);
    }

    std::atomic<uint16_t> _claim{CLAIM_IDLE};
    std::atomic<uint8_t> _failure{
        static_cast<uint8_t>(RuntimeOperationFailure::NONE)};
    std::atomic<uint32_t> _lastProgressMs{0};
    std::atomic<uint32_t> _timeoutMs{0};
};
