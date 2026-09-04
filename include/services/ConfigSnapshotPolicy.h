#ifndef CONFIG_SNAPSHOT_POLICY_H
#define CONFIG_SNAPSHOT_POLICY_H

#include <cstring>

#include "services/ConfigImportValidation.h"
#include "services/SensitiveDataRedaction.h"

// Snapshoty leží v nešifrovaném LittleFS — tajemství se maskují už při
// uložení (docs/PHYSICAL_SECURITY_HARDENING_CZ.md §6.5). Slot si nese jen
// informaci, ŽE bylo tajemství nakonfigurované, ne jeho hodnotu.
static constexpr const char* CONFIG_SNAPSHOT_REDACTED = "***";

// Parita s /api/config/export: credential tokeny (pass/token/pin/webhook…)
// plus obě user-poloviny páru, které tokenová detekce sama nechytí.
inline bool configSnapshotKeyIsSecret(const char* key) {
    if (key == nullptr) return false;
    return isSensitiveKeyName(key) ||
           std::strcmp(key, "auth_user") == 0 ||
           std::strcmp(key, "mqtt_user") == 0;
}

// Hodnota k zápisu do slotu: neprázdné tajemství -> sentinel, prázdná
// hodnota zůstává prázdná, aby slot ukazoval "nenakonfigurováno".
inline const char* configSnapshotMaskedString(const char* key, const char* value) {
    if (value == nullptr || value[0] == '\0') return value;
    return configSnapshotKeyIsSecret(key) ? CONFIG_SNAPSHOT_REDACTED : value;
}

// Restore nesmí sentinelem přepsat provisionované tajemství v NVS; raw
// hodnoty ze starších snapshotů se obnovují beze změny.
inline bool configSnapshotShouldRestoreString(const char* value) {
    return value != nullptr && !configImportValueIsRedacted(value);
}

#endif
