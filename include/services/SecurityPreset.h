#ifndef SECURITY_PRESET_H
#define SECURITY_PRESET_H

// #12 Security sensitivity preset (Empty/Home/Paranoid) maps to a coherent
// set of alarm knobs. It is distinct from the radar-tuning /api/preset.
// Pure/Arduino-free so the mapping is covered by native tests.
#include <cstdint>
#include <cstring>

enum class SecPreset : uint8_t {
    EMPTY,
    HOME,
    PARANOID,
};

struct SecPresetParams {
    uint8_t alarmEnergyThreshold;
    uint32_t entryDelayMs;
    uint8_t petImmunity;
    bool corroborationEnabled;
};

inline SecPresetParams securityPresetParams(SecPreset preset) {
    switch (preset) {
        case SecPreset::EMPTY:
            return {40, 45000, 15, true};
        case SecPreset::PARANOID:
            return {15, 10000, 0, false};
        case SecPreset::HOME:
        default:
            return {30, 30000, 5, false};
    }
}

inline const char* secPresetName(SecPreset preset) {
    switch (preset) {
        case SecPreset::EMPTY:
            return "Empty";
        case SecPreset::PARANOID:
            return "Paranoid";
        case SecPreset::HOME:
        default:
            return "Home";
    }
}

inline bool parseSecPreset(const char* name, SecPreset& out) {
    if (!name) {
        return false;
    }
    if (!strcmp(name, "Empty")) {
        out = SecPreset::EMPTY;
        return true;
    }
    if (!strcmp(name, "Home")) {
        out = SecPreset::HOME;
        return true;
    }
    if (!strcmp(name, "Paranoid")) {
        out = SecPreset::PARANOID;
        return true;
    }
    return false;
}

#endif  // SECURITY_PRESET_H
