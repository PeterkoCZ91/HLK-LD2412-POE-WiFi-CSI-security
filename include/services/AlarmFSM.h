#ifndef ALARM_FSM_H
#define ALARM_FSM_H

#include <cstdint>

// =============================================================================
// AlarmFSM — čistý stavový automat alarmu (IMPROVEMENTS T5/T6)
//
// Vyextrahováno ze SecurityMonitoru tak, aby šlo testovat nativně bez Arduina.
// Drží POUZE stav + časovače + debounce. Fúze (radar+CSI), zóny, static filtr,
// pet-immunity a entry-path override zůstávají v SecurityMonitoru, který:
//   - každý frame spočítá `qualifies` (kvalifikovaný pohyb po fúzi/filtrech)
//     a `behavior` (0=entry delay, 1=immediate, 2=ignore, 3=static-zone≈entry)
//     a zavolá reportMotion(qualifies, behavior, now)
//   - periodicky (a/nebo per-frame) volá tick(now) pro časové přechody
//   - na vrácený event navěsí side-effecty (MQTT event, alert, siréna, persist)
//
// Čas se injektuje jako `now` (millis()), takže testy jsou deterministické.
// Rozdíly se počítají přes unsigned wrap-around: (uint32_t)(now - start).
// =============================================================================

enum class AlarmState : uint8_t {
    DISARMED,
    ARMING,     // probíhá exit delay
    ARMED,
    PENDING,    // probíhá entry delay
    TRIGGERED
};

// Výsledek arm()
enum class ArmResult : uint8_t {
    REJECTED,         // odmítnuto (PENDING/TRIGGERED) — beze změny
    IDEMPOTENT,       // už ARMING/ARMED — beze změny stavu (homeMode řeší host)
    ARMING_STARTED,   // DISARMED -> ARMING (běží exit delay)
    ARMED_IMMEDIATE   // DISARMED -> ARMED (bez exit delay)
};

// Výsledek tick()
enum class TickEvent : uint8_t {
    NONE,
    EXIT_DELAY_DONE,          // ARMING -> ARMED
    ENTRY_EXPIRED_TRIGGERED,  // PENDING -> TRIGGERED
    AUTO_REARMED,             // TRIGGERED -> ARMED (timeout, autoRearm)
    AUTO_DISARMED             // TRIGGERED -> DISARMED (timeout, !autoRearm)
};

// Výsledek reportMotion()
enum class MotionEvent : uint8_t {
    NONE,
    PENDING_STARTED,      // ARMED -> PENDING (entry delay)
    TRIGGERED_IMMEDIATE,  // ARMED -> TRIGGERED (immediate behavior)
    IGNORED               // behavior==ignore — debounce splněn, ale beze změny
};

class AlarmFSM {
public:
    // Konfigurace (ms) — defaulty odpovídají SecurityMonitoru
    uint32_t entryDelayMs     = 30000;
    uint32_t exitDelayMs      = 30000;
    uint32_t triggerTimeoutMs = 900000;  // 0 = auto-silence vypnuto
    bool     autoRearm        = true;
    uint8_t  debounceFrames   = 3;       // clampnuto na >=1 v reportMotion

    AlarmState state() const { return _state; }

    // Odpovídá SecurityMonitor::setArmed(true, immediate).
    ArmResult arm(bool immediate, uint32_t now) {
        if (_state == AlarmState::PENDING || _state == AlarmState::TRIGGERED)
            return ArmResult::REJECTED;
        if (_state == AlarmState::ARMING || _state == AlarmState::ARMED)
            return ArmResult::IDEMPOTENT;
        _debounceCount = 0;
        if (immediate) {
            _state = AlarmState::ARMED;
            return ArmResult::ARMED_IMMEDIATE;
        }
        _state = AlarmState::ARMING;
        _exitStart = now;
        return ArmResult::ARMING_STARTED;
    }

    // Odpovídá SecurityMonitor::setArmed(false). Vrací true, pokud došlo ke změně
    // (host pak vyšle "disarmed" event).
    bool disarm(uint32_t /*now*/) {
        bool changed = (_state != AlarmState::DISARMED);
        _state = AlarmState::DISARMED;
        _exitStart = _entryStart = _triggerStart = 0;
        _debounceCount = 0;
        return changed;
    }

    // Časové přechody. Odpovídá update() (exit delay, trigger timeout) a
    // entry-delay expiraci z processRadarData. Stavy jsou vzájemně výlučné,
    // takže za jeden tick nastane nejvýše jeden přechod.
    TickEvent tick(uint32_t now) {
        if (_state == AlarmState::ARMING &&
            (uint32_t)(now - _exitStart) >= exitDelayMs) {
            _state = AlarmState::ARMED;
            return TickEvent::EXIT_DELAY_DONE;
        }
        if (_state == AlarmState::TRIGGERED && triggerTimeoutMs > 0 &&
            (uint32_t)(now - _triggerStart) >= triggerTimeoutMs) {
            if (autoRearm) { _state = AlarmState::ARMED; return TickEvent::AUTO_REARMED; }
            _state = AlarmState::DISARMED;
            return TickEvent::AUTO_DISARMED;
        }
        if (_state == AlarmState::PENDING &&
            (uint32_t)(now - _entryStart) >= entryDelayMs) {
            _state = AlarmState::TRIGGERED;
            _triggerStart = now;
            return TickEvent::ENTRY_EXPIRED_TRIGGERED;
        }
        return TickEvent::NONE;
    }

    // Kvalifikovaný/ne pohyb v daném framu. `behavior`: 0=entry delay,
    // 1=immediate, 2=ignore, 3=static-zone (≈entry delay). Debounce vyžaduje
    // `debounceFrames` po sobě jdoucích kvalifikujících framů. Účinné jen v ARMED.
    MotionEvent reportMotion(bool qualifies, uint8_t behavior, uint32_t now) {
        if (_state != AlarmState::ARMED) return MotionEvent::NONE;
        const uint8_t frames = (debounceFrames < 1) ? 1 : debounceFrames;
        if (!qualifies) { _debounceCount = 0; return MotionEvent::NONE; }
        if (_debounceCount < 255) _debounceCount++;
        if (_debounceCount < frames) return MotionEvent::NONE;
        _debounceCount = 0;
        if (behavior == 2) return MotionEvent::IGNORED;
        if (behavior == 1) {
            _state = AlarmState::TRIGGERED;
            _triggerStart = now;
            return MotionEvent::TRIGGERED_IMMEDIATE;
        }
        _state = AlarmState::PENDING;   // behavior 0 i 3
        _entryStart = now;
        return MotionEvent::PENDING_STARTED;
    }

    // Pro host/persist: aktuální debounce počítadlo.
    uint8_t debounceCount() const { return _debounceCount; }

private:
    AlarmState _state = AlarmState::DISARMED;
    uint32_t _exitStart = 0;
    uint32_t _entryStart = 0;
    uint32_t _triggerStart = 0;
    uint8_t  _debounceCount = 0;
};

#endif // ALARM_FSM_H
