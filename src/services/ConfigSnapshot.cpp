#include "services/ConfigSnapshot.h"
#include "debug.h"
#include <time.h>

// -------------------------------------------------------------------------
// NVS keys to snapshot
// Format: { key, type }  types: s=string, u=uint32, b=bool, f=float, uc=uint8
// -------------------------------------------------------------------------
struct NvsKeyDef {
    const char* key;
    char        type;   // 's', 'u', 'b', 'f', 'c'  (c = uchar/uint8)
};

static const NvsKeyDef NVS_KEYS[] = {
    // --- ConfigManager keys ---
    { "mqtt_server",    's' },
    { "mqtt_port",      'u' },
    { "mqtt_user",      's' },
    { "mqtt_pass",      's' },
    { "mqtt_id",        's' },
    { "hostname",       's' },
    { "auth_user",      's' },
    { "auth_pass",      's' },
    { "mqtt_en",        'b' },
    { "led_en",         'b' },
    { "led_start",      'b' },
    { "radar_res",      'f' },
    // --- main.cpp keys ---
    { "hold_time",      'u' },
    { "sec_pet",        'u' },
    { "radar_min",      'u' },
    { "radar_max",      'u' },
    { "sec_antimask",   'u' },
    { "sec_am_en",      'b' },
    { "sec_loiter",     'u' },
    { "sec_loit_en",    'b' },
    { "sec_hb",         'u' },
    { "sec_entry_dl",   'u' },
    { "sec_exit_dl",    'u' },
    { "sec_dis_rem",    'b' },
    { "sec_trig_to",    'u' },
    { "sec_auto_rearm", 'b' },
    { "sec_alarm_en",   'c' },
    { "sec_armed",      'b' },
    { "zones_json",     's' },
};
static constexpr size_t NVS_KEY_COUNT = sizeof(NVS_KEYS) / sizeof(NVS_KEYS[0]);

// -------------------------------------------------------------------------

void ConfigSnapshot::begin() {
    loadMeta();
    DBG("ConfigSnapshot", "Ready — %u snapshot(s), newest slot %d", _snapshotCount, _newestSlot);
}

const char* ConfigSnapshot::_slotPath(int slot) {
    snprintf(_pathBuf, sizeof(_pathBuf), "/cfg_snap_%d.json", slot);
    return _pathBuf;
}

// -------------------------------------------------------------------------
// Load metadata (which slot is newest, count)
// -------------------------------------------------------------------------
bool ConfigSnapshot::loadMeta() {
    if (!LittleFS.exists(SNAPSHOT_META)) return false;

    File f = LittleFS.open(SNAPSHOT_META, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    _newestSlot    = doc["newest"] | -1;
    _snapshotCount = doc["count"]  | 0;
    return true;
}

void ConfigSnapshot::saveMeta() {
    File f = LittleFS.open(SNAPSHOT_META, "w");
    if (!f) return;
    JsonDocument doc;
    doc["newest"] = _newestSlot;
    doc["count"]  = _snapshotCount;
    serializeJson(doc, f);
    f.close();
}

// -------------------------------------------------------------------------
// saveSnapshot — serialize all NVS keys to next slot file
// -------------------------------------------------------------------------
bool ConfigSnapshot::saveSnapshot(Preferences* prefs, const char* fwVersion, const char* reason) {
    if (!prefs) return false;

    int slot = (_newestSlot + 1) % SNAPSHOT_SLOTS;
    const char* path = _slotPath(slot);

    File f = LittleFS.open(path, "w");
    if (!f) {
        DBG("ConfigSnapshot", "Cannot open %s for write", path);
        return false;
    }

    JsonDocument doc;
    time_t epoch = time(nullptr);
    doc["ts"]  = (epoch > 1700000000) ? (uint32_t)epoch : millis() / 1000;
    doc["fw"]  = fwVersion ? fwVersion : "unknown";
    doc["reason"] = reason ? reason : "ota";

    JsonObject cfg = doc["cfg"].to<JsonObject>();

    for (size_t i = 0; i < NVS_KEY_COUNT; i++) {
        const char* k = NVS_KEYS[i].key;
        if (!prefs->isKey(k)) continue;

        switch (NVS_KEYS[i].type) {
            case 's': cfg[k] = prefs->getString(k, "");         break;
            case 'u': cfg[k] = prefs->getULong(k, 0);          break;
            case 'b': cfg[k] = prefs->getBool(k, false);        break;
            case 'f': cfg[k] = prefs->getFloat(k, 0.0f);        break;
            case 'c': cfg[k] = prefs->getUChar(k, 0);           break;
        }
    }

    size_t written = serializeJson(doc, f);
    f.close();

    if (written == 0) {
        DBG("ConfigSnapshot", "Serialization failed for slot %d", slot);
        return false;
    }

    _newestSlot = slot;
    if (_snapshotCount < SNAPSHOT_SLOTS) _snapshotCount++;
    saveMeta();

    DBG("ConfigSnapshot", "Saved slot %d (%u bytes, fw=%s, reason=%s)", slot, written, fwVersion, reason);
    return true;
}

// -------------------------------------------------------------------------
// restoreSnapshot — write keys back to NVS from a slot file
// -------------------------------------------------------------------------
bool ConfigSnapshot::restoreSnapshot(Preferences* prefs, int slot) {
    if (!prefs) return false;
    if (slot < 0) slot = _newestSlot;
    if (slot < 0 || slot >= SNAPSHOT_SLOTS) return false;

    const char* path = _slotPath(slot);
    File f = LittleFS.open(path, "r");
    if (!f) {
        DBG("ConfigSnapshot", "Slot %d not found", slot);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        DBG("ConfigSnapshot", "Parse error slot %d: %s", slot, err.c_str());
        return false;
    }

    JsonObject cfg = doc["cfg"].as<JsonObject>();
    if (cfg.isNull()) return false;

    uint16_t restored = 0;
    for (size_t i = 0; i < NVS_KEY_COUNT; i++) {
        const char* k = NVS_KEYS[i].key;
        if (cfg[k].isNull()) continue;

        switch (NVS_KEYS[i].type) {
            case 's': prefs->putString(k, cfg[k].as<const char*>()); break;
            case 'u': prefs->putULong(k,  cfg[k].as<uint32_t>());    break;
            case 'b': prefs->putBool(k,   cfg[k].as<bool>());         break;
            case 'f': prefs->putFloat(k,  cfg[k].as<float>());        break;
            case 'c': prefs->putUChar(k,  cfg[k].as<uint8_t>());      break;
        }
        restored++;
    }

    DBG("ConfigSnapshot", "Restored %u keys from slot %d (fw=%s)", restored, slot,
        doc["fw"] | "?");
    return true;
}

// -------------------------------------------------------------------------
// getMetaJSON — list of all available snapshots
// -------------------------------------------------------------------------
void ConfigSnapshot::getMetaJSON(JsonDocument& doc) {
    JsonObject root = doc.to<JsonObject>();
    root["newest"] = _newestSlot;
    root["count"]  = _snapshotCount;
    root["slots"]  = SNAPSHOT_SLOTS;

    JsonArray arr = root["snapshots"].to<JsonArray>();

    for (int s = 0; s < SNAPSHOT_SLOTS; s++) {
        const char* path = _slotPath(s);
        if (!LittleFS.exists(path)) continue;

        File f = LittleFS.open(path, "r");
        if (!f) continue;

        JsonDocument slotDoc;
        if (deserializeJson(slotDoc, f) == DeserializationError::Ok) {
            JsonObject info = arr.add<JsonObject>();
            info["slot"]   = s;
            info["ts"]     = slotDoc["ts"] | 0;
            info["fw"]     = slotDoc["fw"] | "?";
            info["reason"] = slotDoc["reason"] | "?";
            info["size"]   = f.size();
        }
        f.close();
    }
}

// -------------------------------------------------------------------------
// getSnapshotJSON — full content of one slot (for download / inspect)
// Masks sensitive values
// -------------------------------------------------------------------------
bool ConfigSnapshot::getSnapshotJSON(JsonDocument& doc, int slot) {
    if (slot < 0) slot = _newestSlot;
    if (slot < 0 || slot >= SNAPSHOT_SLOTS) return false;

    const char* path = _slotPath(slot);
    File f = LittleFS.open(path, "r");
    if (!f) return false;

    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    // Mask sensitive keys in-place
    if (doc["cfg"].is<JsonObject>()) {
        JsonObject cfg = doc["cfg"].as<JsonObject>();
        if (!cfg["mqtt_pass"].isNull())  cfg["mqtt_pass"]  = "***";
        if (!cfg["auth_pass"].isNull())  cfg["auth_pass"]  = "***";
    }
    return true;
}
