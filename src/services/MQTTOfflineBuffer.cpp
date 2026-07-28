#include "services/MQTTOfflineBuffer.h"
#include "debug.h"
#include <time.h>

// -------------------------------------------------------------------------
// begin — validate or create the disk file
// -------------------------------------------------------------------------
void MQTTOfflineBuffer::begin() {
    if (!LittleFS.begin(false)) {
        // LittleFS should already be mounted by main.cpp; just check
        DBG("MQTTBuf", "LittleFS not mounted");
        return;
    }
    _fsAvailable = true;

    if (LittleFS.exists(_filename)) {
        // Read existing header
        File f = LittleFS.open(_filename, "r");
        if (f) {
            MQTTBufHeader hdr;
            if (f.read((uint8_t*)&hdr, MQTT_BUF_HDR_SIZE) == MQTT_BUF_HDR_SIZE &&
                hdr.magic == MQTT_BUF_MAGIC && hdr.capacity == MQTT_BUF_CAPACITY) {
                _head  = hdr.head;
                _count = (hdr.count > MQTT_BUF_CAPACITY) ? MQTT_BUF_CAPACITY : hdr.count;
                f.close();
                DBG("MQTTBuf", "Loaded — %u buffered messages", _count);
                return;
            }
            f.close();
        }
        // Corrupt/incompatible — recreate
        LittleFS.remove(_filename);
    }

    initFile();
}

// -------------------------------------------------------------------------
// initFile — pre-allocate the ring buffer file
// -------------------------------------------------------------------------
bool MQTTOfflineBuffer::initFile() {
    File f = LittleFS.open(_filename, "w");
    if (!f) {
        DBG("MQTTBuf", "Cannot create %s", _filename);
        _fsAvailable = false;
        return false;
    }

    MQTTBufHeader hdr = {};
    hdr.magic    = MQTT_BUF_MAGIC;
    hdr.capacity = MQTT_BUF_CAPACITY;
    hdr.count    = 0;
    hdr.head     = 0;
    f.write((const uint8_t*)&hdr, MQTT_BUF_HDR_SIZE);

    BufferedMsg empty = {};
    for (size_t i = 0; i < MQTT_BUF_CAPACITY; i++) {
        f.write((const uint8_t*)&empty, MQTT_BUF_MSG_SIZE);
    }
    f.close();

    _head  = 0;
    _count = 0;
    DBG("MQTTBuf", "File created: %u slots (%u bytes)",
        (unsigned)MQTT_BUF_CAPACITY, (unsigned)(MQTT_BUF_HDR_SIZE + MQTT_BUF_CAPACITY * MQTT_BUF_MSG_SIZE));
    return true;
}

// -------------------------------------------------------------------------
// updateHeader — persist head/count to disk
// -------------------------------------------------------------------------
bool MQTTOfflineBuffer::updateHeader() {
    File f = LittleFS.open(_filename, "r+");
    if (!f) return false;

    MQTTBufHeader hdr;
    hdr.magic    = MQTT_BUF_MAGIC;
    hdr.capacity = MQTT_BUF_CAPACITY;
    hdr.count    = _count;
    hdr.head     = _head;

    f.seek(0);
    f.write((const uint8_t*)&hdr, MQTT_BUF_HDR_SIZE);
    f.close();
    return true;
}

// -------------------------------------------------------------------------
// store — write one message to the next ring slot
// Skip HA discovery messages (homeassistant/ prefix) — they're re-sent on reconnect anyway
// -------------------------------------------------------------------------
bool MQTTOfflineBuffer::store(const char* topic, const char* payload, bool retained,
                              uint64_t eventId) {
    if (!_fsAvailable || !topic || !payload) return false;

    // Don't buffer HA Discovery — they're replayed via publishDiscoveryStep on reconnect
    if (strncmp(topic, "homeassistant/", 14) == 0) return false;

    if (strlen(topic)   >= MQTT_BUF_MSG_SIZE /* sanity */   ) return false;
    if (strlen(topic)   >= sizeof(BufferedMsg::topic)   - 1) return false;
    if (strlen(payload) >= sizeof(BufferedMsg::payload) - 1) return false;

    // Do not let high-rate retained telemetry consume the whole shared ring.
    // A non-zero eventId is an alarm/security event and may use the reserved
    // tail; telemetry is intentionally shed before it can evict that event.
    if (eventId == 0 && _count >= MQTT_BUF_CAPACITY - MQTT_BUF_EVENT_RESERVE) {
        DBG("MQTTBuf", "Telemetry shed; reserving %u slots for security events",
            (unsigned)MQTT_BUF_EVENT_RESERVE);
        return false;
    }

    // Open or create file
    if (!LittleFS.exists(_filename)) {
        if (!initFile()) return false;
    }

    if (eventId != 0 && containsEventId(eventId)) {
        DBG("MQTTBuf", "Duplicate event skipped: %08x%08x",
            (unsigned)(eventId >> 32), (unsigned)eventId);
        return true;
    }

    File f = LittleFS.open(_filename, "r+");
    if (!f) return false;

    // BA-14: determine the slot WITHOUT mutating ring state yet — only advance
    // head/count after the bytes are verified on disk (same guard as EventLog
    // BA-13). Before, the counters advanced first and the seek()/write() returns
    // were ignored, so a short write (full/failing LittleFS) left a phantom slot
    // counted while the message was never persisted.
    bool willWrap = (_count >= MQTT_BUF_CAPACITY);
    uint32_t writeIdx = willWrap ? _head : (_head + _count) % MQTT_BUF_CAPACITY;

    BufferedMsg msg = {};
    msg.event_id = eventId;
    time_t epoch = time(nullptr);
    msg.timestamp = (epoch > 1700000000) ? (uint32_t)epoch : millis() / 1000;
    msg.retained  = retained ? 1 : 0;
    strncpy(msg.topic,   topic,   sizeof(msg.topic)   - 1);
    strncpy(msg.payload, payload, sizeof(msg.payload) - 1);

    size_t offset = MQTT_BUF_HDR_SIZE + writeIdx * MQTT_BUF_MSG_SIZE;
    if (!f.seek(offset)) { f.close(); return false; }
    size_t written = f.write((const uint8_t*)&msg, MQTT_BUF_MSG_SIZE);
    f.close();
    if (written != MQTT_BUF_MSG_SIZE) return false;   // ring state untouched

    uint32_t oldHead = _head;
    uint32_t oldCount = _count;
    if (willWrap) _head = (_head + 1) % MQTT_BUF_CAPACITY;
    else          _count++;

    if (!updateHeader()) {
        _head = oldHead;
        _count = oldCount;
        return false;
    }
    DBG("MQTTBuf", "Stored [%u/%u]: %s", _count, (unsigned)MQTT_BUF_CAPACITY, topic);
    return true;
}

bool MQTTOfflineBuffer::containsEventId(uint64_t eventId) {
    if (eventId == 0 || _count == 0) return false;

    File f = LittleFS.open(_filename, "r");
    if (!f) return false;

    BufferedMsg msg;
    for (uint32_t i = 0; i < _count; i++) {
        uint32_t physIdx = (_head + i) % MQTT_BUF_CAPACITY;
        size_t offset = MQTT_BUF_HDR_SIZE + physIdx * MQTT_BUF_MSG_SIZE;
        if (!f.seek(offset) ||
            f.read((uint8_t*)&msg, MQTT_BUF_MSG_SIZE) != MQTT_BUF_MSG_SIZE) {
            f.close();
            return false;
        }
        if (mqttBufferedEventIdsMatch(msg.event_id, eventId)) {
            f.close();
            return true;
        }
    }
    f.close();
    return false;
}

// -------------------------------------------------------------------------
// readMsg — read one message by logical index (0 = oldest)
// -------------------------------------------------------------------------
bool MQTTOfflineBuffer::readMsg(uint32_t index, BufferedMsg& out) {
    if (index >= _count) return false;

    File f = LittleFS.open(_filename, "r");
    if (!f) return false;

    uint32_t physIdx = (_head + index) % MQTT_BUF_CAPACITY;
    size_t offset = MQTT_BUF_HDR_SIZE + physIdx * MQTT_BUF_MSG_SIZE;
    f.seek(offset);
    bool ok = (f.read((uint8_t*)&out, MQTT_BUF_MSG_SIZE) == MQTT_BUF_MSG_SIZE);
    f.close();
    return ok;
}

// -------------------------------------------------------------------------
// replay — publish all buffered messages oldest-first
// Stops on first publish failure (network issue); clears buffer on full success
// Returns number of successfully replayed messages
// -------------------------------------------------------------------------
uint16_t MQTTOfflineBuffer::replay(ReplayFn publishFn) {
    if (!_fsAvailable || _count == 0 || !publishFn) return 0;

    uint32_t total = _count;
    uint16_t sent  = 0;

    DBG("MQTTBuf", "Replaying %u buffered messages...", total);

    for (uint32_t i = 0; i < total; i++) {
        BufferedMsg msg;
        if (!readMsg(i, msg)) {
            // Keep the first two attempts non-destructive for transient FS
            // faults. A persistently unreadable head would otherwise block all
            // newer messages forever, so skip just that one record on attempt 3.
            _replayReadFailureStreak++;
            _head  = (_head + sent) % MQTT_BUF_CAPACITY;
            _count = total - sent;
            if (_replayReadFailureStreak >= 3) {
                _head = (_head + 1) % MQTT_BUF_CAPACITY;
                _count--;
                _replayReadFailureStreak = 0;
                DBG("MQTTBuf", "Skipped persistently unreadable msg %u", i);
            } else {
                DBG("MQTTBuf", "Replay stopped at msg %u — read failed (%u/3)",
                    i, (unsigned)_replayReadFailureStreak);
            }
            updateHeader();
            return sent;
        }

        _replayReadFailureStreak = 0;

        if (!publishFn(msg.topic, msg.payload, msg.retained)) {
            DBG("MQTTBuf", "Replay stopped at msg %u — publish failed", i);
            // Shift remaining messages to front and update head/count
            _head  = (_head + sent) % MQTT_BUF_CAPACITY;
            _count = total - sent;
            updateHeader();
            return sent;
        }
        sent++;
    }

    // All replayed — clear buffer
    clear();
    _replayReadFailureStreak = 0;
    DBG("MQTTBuf", "Replayed %u messages, buffer cleared", sent);
    return sent;
}

// -------------------------------------------------------------------------
// clear — reset ring buffer (header only, no need to zero slots)
// -------------------------------------------------------------------------
void MQTTOfflineBuffer::clear() {
    _head  = 0;
    _count = 0;
    updateHeader();
}
