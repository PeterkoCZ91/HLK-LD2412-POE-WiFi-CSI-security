#ifndef AUTH_LOCKOUT_H
#define AUTH_LOCKOUT_H

#include <stdint.h>

/**
 * Per-IP brute-force lockout pro HTTP auth (IMPROVEMENTS T3).
 *
 * Čistá logika bez Arduino závislostí — unit testy v test/test_auth_lockout
 * (pio test -e native). Časové vstupy jsou millis() hodnoty; aritmetika je
 * wraparound-safe přes signed diffy ((int32_t)(a - b)).
 *
 * Chování: FAIL_THRESHOLD neúspěchů uvnitř WINDOW_MS → lock na BASE_LOCK_MS,
 * každý další lockout se zdvojnásobuje až po MAX_LOCK_MS. Úspěšné přihlášení
 * záznam IP smaže. Tabulka má MAX_ENTRIES slotů s LRU evikcí — vytlačení
 * zamčeného záznamu lockout zruší; to je vědomý tradeoff (bounded RAM,
 * 8+ souběžně útočících LAN IP je mimo threat model tohoto zařízení).
 */
class AuthLockout {
public:
    static constexpr uint8_t  MAX_ENTRIES    = 8;
    static constexpr uint8_t  FAIL_THRESHOLD = 5;
    static constexpr uint32_t WINDOW_MS      = 60UL * 1000UL;    // okno čítání neúspěchů
    static constexpr uint32_t BASE_LOCK_MS   = 30UL * 1000UL;    // první lockout
    static constexpr uint32_t MAX_LOCK_MS    = 900UL * 1000UL;   // strop 15 min
    static constexpr uint32_t ENTRY_TTL_MS   = 1800UL * 1000UL;  // zapomenout IP po 30 min klidu

    /** Zbývající ms lockoutu, 0 = povoleno. */
    uint32_t lockedForMs(uint32_t ip, uint32_t nowMs) {
        Entry* e = find(ip, nowMs);
        if (e == nullptr || !e->locked) return 0;
        int32_t remain = (int32_t)(e->lockUntilMs - nowMs);
        if (remain <= 0) { e->locked = false; return 0; }
        return (uint32_t)remain;
    }

    /** Zaznamenat neúspěšný pokus (volat jen když request nesl credentials). */
    void onFailure(uint32_t ip, uint32_t nowMs) {
        Entry* e = findOrCreate(ip, nowMs);
        if ((int32_t)(nowMs - e->windowStartMs) > (int32_t)WINDOW_MS) {
            e->failCount = 0;
            e->windowStartMs = nowMs;
        }
        e->lastSeenMs = nowMs;
        e->failCount++;
        if (e->failCount >= FAIL_THRESHOLD) {
            uint8_t shift = (e->lockStreak > 5) ? 5 : e->lockStreak;
            uint32_t lockMs = BASE_LOCK_MS << shift;  // 30s,60s,120s,240s,480s,960s→cap
            if (lockMs > MAX_LOCK_MS) lockMs = MAX_LOCK_MS;
            if (e->lockStreak < 250) e->lockStreak++;
            e->locked = true;
            e->lockUntilMs = nowMs + lockMs;
            e->failCount = 0;
            e->windowStartMs = nowMs;
        }
    }

    /** Úspěšné přihlášení — záznam IP zahodit (resetuje i backoff streak). */
    void onSuccess(uint32_t ip) {
        for (uint8_t i = 0; i < MAX_ENTRIES; i++) {
            if (_e[i].used && _e[i].ip == ip) { _e[i].used = false; return; }
        }
    }

private:
    struct Entry {
        uint32_t ip = 0;
        uint32_t windowStartMs = 0;
        uint32_t lockUntilMs = 0;
        uint32_t lastSeenMs = 0;
        uint8_t  failCount = 0;
        uint8_t  lockStreak = 0;   // počet dosažených lockoutů (řídí backoff)
        bool     locked = false;
        bool     used = false;
    };
    Entry _e[MAX_ENTRIES];

    Entry* find(uint32_t ip, uint32_t nowMs) {
        for (uint8_t i = 0; i < MAX_ENTRIES; i++) {
            Entry& e = _e[i];
            if (!e.used) continue;
            if ((int32_t)(nowMs - e.lastSeenMs) > (int32_t)ENTRY_TTL_MS) {
                e.used = false;  // TTL úklid mimochodem
                continue;
            }
            if (e.ip == ip) return &e;
        }
        return nullptr;
    }

    Entry* findOrCreate(uint32_t ip, uint32_t nowMs) {
        Entry* e = find(ip, nowMs);
        if (e != nullptr) return e;
        Entry* victim = nullptr;
        for (uint8_t i = 0; i < MAX_ENTRIES; i++) {
            if (!_e[i].used) { victim = &_e[i]; break; }
            if (victim == nullptr ||
                (int32_t)(victim->lastSeenMs - _e[i].lastSeenMs) > 0) {
                victim = &_e[i];  // drž nejstarší (LRU)
            }
        }
        *victim = Entry{};
        victim->used = true;
        victim->ip = ip;
        victim->windowStartMs = nowMs;
        victim->lastSeenMs = nowMs;
        return victim;
    }
};

#endif // AUTH_LOCKOUT_H
