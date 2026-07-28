#ifndef CONFIG_IMPORT_VALIDATION_H
#define CONFIG_IMPORT_VALIDATION_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

inline bool configImportTextFits(const char* value, size_t minLen, size_t maxLen) {
    if (value == nullptr) return false;
    const size_t len = strlen(value);
    return len >= minLen && len <= maxLen;
}

inline bool configImportValueIsRedacted(const char* value) {
    return value != nullptr && strcmp(value, "***") == 0;
}

inline bool configImportPortValid(const char* value) {
    if (!configImportTextFits(value, 1, 5)) return false;
    uint32_t port = 0;
    for (const char* p = value; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        port = port * 10u + static_cast<uint32_t>(*p - '0');
    }
    return port >= 1u && port <= 65535u;
}

inline bool configImportIpv4OrEmptyValid(const char* value) {
    if (value == nullptr) return false;
    if (*value == '\0') return true;

    uint8_t octets = 0;
    const char* p = value;
    while (*p) {
        if (octets == 4) return false;
        uint16_t number = 0;
        uint8_t digits = 0;
        while (*p >= '0' && *p <= '9') {
            number = static_cast<uint16_t>(number * 10u + static_cast<uint16_t>(*p - '0'));
            if (++digits > 3 || number > 255) return false;
            ++p;
        }
        if (digits == 0) return false;
        ++octets;
        if (*p == '\0') break;
        if (*p++ != '.') return false;
        if (*p == '\0') return false;
    }
    return octets == 4;
}

inline bool configImportScheduleValid(const char* value) {
    if (value == nullptr) return false;
    if (*value == '\0') return true;
    if (strlen(value) != 5 || value[2] != ':') return false;
    if (value[0] < '0' || value[0] > '9' ||
        value[1] < '0' || value[1] > '9' ||
        value[3] < '0' || value[3] > '9' ||
        value[4] < '0' || value[4] > '9') return false;
    const uint8_t hour = static_cast<uint8_t>((value[0] - '0') * 10 + value[1] - '0');
    const uint8_t minute = static_cast<uint8_t>((value[3] - '0') * 10 + value[4] - '0');
    return hour <= 23 && minute <= 59;
}

inline bool configImportRadarResolutionValid(float value) {
    return value == 0.20f || value == 0.50f || value == 0.75f;
}

#endif
