#pragma once

#include <stdint.h>
#include <atomic>

enum class TelegramTestState : uint8_t {
    UNKNOWN = 0,
    QUEUED,
    SENDING,
    SUCCEEDED,
    FAILED,
};

inline const char* telegramTestStateText(TelegramTestState state) {
    switch (state) {
        case TelegramTestState::QUEUED: return "queued";
        case TelegramTestState::SENDING: return "sending";
        case TelegramTestState::SUCCEEDED: return "succeeded";
        case TelegramTestState::FAILED: return "failed";
        default: return "unknown";
    }
}

inline bool telegramTestStateTerminal(TelegramTestState state) {
    return state == TelegramTestState::SUCCEEDED || state == TelegramTestState::FAILED;
}

class TelegramTestTracker {
public:
    bool begin(uint32_t requestId) {
        if (requestId == 0) return false;
        uint32_t expected = 0;
        if (!_activeId.compare_exchange_strong(expected, requestId,
                                                std::memory_order_acq_rel)) {
            return false;
        }
        _activeState.store(static_cast<uint8_t>(TelegramTestState::QUEUED),
                           std::memory_order_release);
        return true;
    }

    void markSending(uint32_t requestId) {
        if (_activeId.load(std::memory_order_acquire) == requestId) {
            _activeState.store(static_cast<uint8_t>(TelegramTestState::SENDING),
                               std::memory_order_release);
        }
    }

    void cancel(uint32_t requestId) {
        if (_activeId.load(std::memory_order_acquire) != requestId) return;
        _activeState.store(static_cast<uint8_t>(TelegramTestState::UNKNOWN),
                           std::memory_order_release);
        _activeId.store(0, std::memory_order_release);
    }

    void finish(uint32_t requestId, TelegramTestState state) {
        if (_activeId.load(std::memory_order_acquire) != requestId ||
            !telegramTestStateTerminal(state)) {
            return;
        }
        _lastState.store(static_cast<uint8_t>(state), std::memory_order_release);
        _lastId.store(requestId, std::memory_order_release);
        _activeId.store(0, std::memory_order_release);
    }

    TelegramTestState get(uint32_t requestId) const {
        if (requestId == 0) return TelegramTestState::UNKNOWN;
        if (requestId == _activeId.load(std::memory_order_acquire)) {
            return static_cast<TelegramTestState>(
                _activeState.load(std::memory_order_acquire));
        }
        if (requestId == _lastId.load(std::memory_order_acquire)) {
            return static_cast<TelegramTestState>(
                _lastState.load(std::memory_order_acquire));
        }
        return TelegramTestState::UNKNOWN;
    }

private:
    std::atomic<uint32_t> _activeId{0};
    std::atomic<uint32_t> _lastId{0};
    std::atomic<uint8_t> _activeState{static_cast<uint8_t>(TelegramTestState::UNKNOWN)};
    std::atomic<uint8_t> _lastState{static_cast<uint8_t>(TelegramTestState::UNKNOWN)};
};
