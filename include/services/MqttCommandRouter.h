#ifndef MQTT_COMMAND_ROUTER_H
#define MQTT_COMMAND_ROUTER_H

#include <stdint.h>
#include <string.h>
#include "services/AuthLockout.h"

// Čistá rozhodovací logika pro MQTT `alarm_set` příkaz — ARM_AWAY/ARM_HOME/
// DISARM s volitelným "CMD:pin" PIN guardem (IMPROVEMENTS T6, audit F-01).
// Vyextrahováno z main.cpp MQTT command dispatch lambdy, aby šlo testovat
// nativně bez Arduino/MQTT/Preferences. Volající si drží AuthLockout instanci
// (main.cpp: globální `mqttCmdLockout`) a čas injektuje jako millis().
//
// storedPin: "" (nebo nullptr) znamená "PIN guard vypnutý" — libovolný holý
// příkaz je přijat. Sdílený lockout bucket klíč 0 — MQTT nemá zdrojovou IP,
// na kterou by šlo klíčovat per-odesílatel (stejný tradeoff jako v main.cpp
// dřív).

enum class MqttArmCommand : uint8_t { ArmAway, ArmHome, Disarm, Unknown };

enum class MqttArmDecision : uint8_t {
    Accepted,
    RejectedLockedOut,       // brute-force lockout aktivní
    RejectedNoPin,           // PIN vyžadován, payload žádný nenesl
    RejectedWrongPin,        // PIN vyžadován, nesedí
    RejectedUnknownCommand,  // base příkaz není ARM_AWAY/ARM_HOME/DISARM
};

struct MqttArmResult {
    MqttArmDecision decision;
    MqttArmCommand  command;  // platné jen když decision == Accepted
};

class MqttCommandRouter {
public:
    // payload: syrový MQTT payload, např. "ARM_AWAY" nebo "ARM_AWAY:1234".
    static MqttArmResult evaluateArmCommand(const char* payload, const char* storedPin,
                                             AuthLockout& lockout, uint32_t nowMs) {
        const char* base = payload;
        size_t baseLen = strlen(payload);

        const bool pinRequired = storedPin != nullptr && storedPin[0] != '\0';

        if (pinRequired) {
            // Brute-force lockout: odmítnout dřív, než se vůbec podívá na PIN,
            // aby flood špatných PINů nešel zkoušet na plnou rychlost (S-0b).
            if (lockout.lockedForMs(0, nowMs) > 0) {
                return {MqttArmDecision::RejectedLockedOut, MqttArmCommand::Unknown};
            }
            const char* sep = strchr(payload, ':');
            if (sep == nullptr) {
                // Bez PINu není co uhodnout — nesmí se počítat do lockoutu,
                // jinak by holé ARM/DISARM z Home Assistant (jeho
                // alarm_control_panel discovery je code_*_required:false)
                // ucpalo bucket a zablokovalo legitimní CMD:pin příkazy.
                return {MqttArmDecision::RejectedNoPin, MqttArmCommand::Unknown};
            }
            baseLen = (size_t)(sep - payload);
            const bool pinOk = (strcmp(sep + 1, storedPin) == 0);
            if (pinOk) {
                lockout.onSuccess(0);
            } else {
                // Špatný PIN v tvaru CMD:pin JE pokus o uhodnutí — jediná
                // cesta, která krmí lockout.
                lockout.onFailure(0, nowMs);
                return {MqttArmDecision::RejectedWrongPin, MqttArmCommand::Unknown};
            }
        }

        MqttArmCommand cmd = MqttArmCommand::Unknown;
        if (matches(base, baseLen, "ARM_AWAY")) cmd = MqttArmCommand::ArmAway;
        else if (matches(base, baseLen, "ARM_HOME")) cmd = MqttArmCommand::ArmHome;
        else if (matches(base, baseLen, "DISARM")) cmd = MqttArmCommand::Disarm;

        if (cmd == MqttArmCommand::Unknown) {
            return {MqttArmDecision::RejectedUnknownCommand, MqttArmCommand::Unknown};
        }
        return {MqttArmDecision::Accepted, cmd};
    }

private:
    static bool matches(const char* s, size_t len, const char* lit) {
        return strlen(lit) == len && strncmp(s, lit, len) == 0;
    }
};

#endif  // MQTT_COMMAND_ROUTER_H
