#ifndef CONFIG_SNAPSHOT_H
#define CONFIG_SNAPSHOT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <LittleFS.h>

static constexpr uint8_t SNAPSHOT_SLOTS = 3;
static constexpr const char* SNAPSHOT_META = "/cfg_snap_meta.json";

/**
 * Config Snapshot — saves all NVS config keys to LittleFS before OTA.
 * Keeps 3 rotating slots: /cfg_snap_0.json, /cfg_snap_1.json, /cfg_snap_2.json
 */
class ConfigSnapshot {
public:
    ConfigSnapshot() = default;

    // Call once LittleFS is mounted
    void begin();

    // Serialize all NVS keys to next slot (rotates 0→1→2→0)
    bool saveSnapshot(Preferences* prefs, const char* fwVersion, const char* reason = "ota");

    // Restore NVS from a slot (-1 = newest)
    bool restoreSnapshot(Preferences* prefs, int slot = -1);

    // Fill doc with snapshot metadata (slot list, timestamps, fw versions)
    void getMetaJSON(JsonDocument& doc);

    // Fill doc with content of a single slot
    bool getSnapshotJSON(JsonDocument& doc, int slot);

    int getNewestSlot() const { return _newestSlot; }
    bool hasSnapshots() const { return _snapshotCount > 0; }

private:
    int  _newestSlot = -1;
    uint8_t _snapshotCount = 0;

    const char* _slotPath(int slot);
    bool loadMeta();
    void saveMeta();

    char _pathBuf[20];
};

#endif
