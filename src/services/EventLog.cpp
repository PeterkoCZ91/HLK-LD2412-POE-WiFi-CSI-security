#include "services/EventLog.h"
#include "debug.h"
#include <time.h>
#include <new>

EventLog::EventLog(size_t ramCapacity)
    : _ramCapacity(ramCapacity), _head(0), _count(0)
{
    _buffer = new LogEvent[_ramCapacity];
    _mutex = xSemaphoreCreateMutex();
}

EventLog::~EventLog() {
    delete[] _buffer;
}

void EventLog::begin(bool fsAvailable) {
    _fsAvailable = fsAvailable;
    if (_fsAvailable) {
        loadFromDisk();
    }
    DBG("EventLog", "Ready — disk: %u/%u events, RAM buffer: %u, fs=%d",
        _diskCount, DISK_CAPACITY, _ramCapacity, _fsAvailable);
}

// -------------------------------------------------------------------------
// Add event — goes to RAM buffer, marked dirty for next flush
// -------------------------------------------------------------------------
void EventLog::addEvent(uint8_t type, uint16_t dist, uint8_t energy, const char* msg) {
    LogEvent evt;
    time_t epoch = time(nullptr);
    evt.timestamp = (epoch > 1700000000) ? (uint32_t)epoch : millis() / 1000;
    evt.type = type;
    evt.distance = dist;
    evt.energy = energy;
    strncpy(evt.message, msg, sizeof(evt.message) - 1);
    evt.message[sizeof(evt.message) - 1] = '\0';

    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    size_t index = (_head + _count) % _ramCapacity;
    if (_count < _ramCapacity) {
        _buffer[index] = evt;
        _count++;
    } else {
        _buffer[_head] = evt;
        _head = (_head + 1) % _ramCapacity;
    }
    _dirty = true;
    _ramSequence++;

    xSemaphoreGive(_mutex);
    DBG("EventLog", "Added: %s", msg);
}

// -------------------------------------------------------------------------
// Flush — rate-limited to 60s
// -------------------------------------------------------------------------
void EventLog::flush() {
    unsigned long now = millis();
    if (now - _lastFlush < 60000) return;
    if (!_dirty || !_fsAvailable) return;
    flushToDisk();
}

// -------------------------------------------------------------------------
// Immediate flush (for critical events)
// -------------------------------------------------------------------------
void EventLog::flushNow() {
    if (!_dirty || !_fsAvailable) return;
    flushToDisk();
}

// -------------------------------------------------------------------------
// flushToDisk — write new RAM events to disk ring buffer
// -------------------------------------------------------------------------
void EventLog::flushToDisk() {
    if (!_mutex) return;

    // Snapshot RAM buffer under mutex
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return;

    size_t count = _count;
    size_t head = _head;
    uint32_t seq = _ramSequence;
    // Only flush events added since last flush
    uint32_t newEvents = seq - _lastFlushedSeq;
    if (newEvents == 0) {
        xSemaphoreGive(_mutex);
        return;
    }
    if (newEvents > count) newEvents = count; // cap to buffer size

    // Copy the newest newEvents from RAM buffer
    LogEvent* snapshot = new(std::nothrow) LogEvent[newEvents];
    if (!snapshot) {
        DBG("EventLog", "CRIT: alloc failed heap=%u maxAlloc=%u",
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        xSemaphoreGive(_mutex);
        // Defer the next attempt by another flush interval. Without this,
        // flush() would re-enter on every loop tick (the 60 s rate-limit
        // gate uses _lastFlush, which is only bumped on success below) and
        // re-take the mutex under starvation, blocking addEvent() callers.
        _lastFlush = millis();
        return;
    }
    for (uint32_t i = 0; i < newEvents; i++) {
        // newest events are at the end of the ring
        size_t ramIdx = (head + count - newEvents + i) % _ramCapacity;
        snapshot[i] = _buffer[ramIdx];
    }
    xSemaphoreGive(_mutex);

    // Write each new event to disk
    for (uint32_t i = 0; i < newEvents; i++) {
        if (!writeEventToDisk(snapshot[i])) {
            delete[] snapshot;
            return;
        }
    }
    delete[] snapshot;

    updateDiskHeader();
    _lastFlush = millis();

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (_ramSequence == seq) _dirty = false;
        _lastFlushedSeq = seq;
        xSemaphoreGive(_mutex);
    }

    DBG("EventLog", "Flushed %u events to disk (total: %u/%u)", newEvents, _diskCount, DISK_CAPACITY);
}

// -------------------------------------------------------------------------
// Write single event to disk ring at next slot
// -------------------------------------------------------------------------
bool EventLog::writeEventToDisk(const LogEvent& evt) {
    File f = LittleFS.open(_filename, "r+");
    if (!f) {
        // File doesn't exist yet — create it
        initDiskFile();
        f = LittleFS.open(_filename, "r+");
        if (!f) return false;
    }

    // Calculate write position
    uint32_t writeIdx;
    if (_diskCount < DISK_CAPACITY) {
        writeIdx = (_diskHead + _diskCount) % DISK_CAPACITY;
        _diskCount++;
    } else {
        writeIdx = _diskHead;
        _diskHead = (_diskHead + 1) % DISK_CAPACITY;
    }

    size_t offset = HEADER_SIZE + writeIdx * EVENT_SIZE;
    f.seek(offset);
    f.write((const uint8_t*)&evt, EVENT_SIZE);
    f.close();

    _diskSequence++;
    return true;
}

// -------------------------------------------------------------------------
// Read single event from disk by index (0 = oldest)
// -------------------------------------------------------------------------
bool EventLog::readEventFromDisk(size_t index, LogEvent& out) {
    if (index >= _diskCount) return false;

    File f = LittleFS.open(_filename, "r");
    if (!f) return false;

    uint32_t physIdx = (_diskHead + index) % DISK_CAPACITY;
    size_t offset = HEADER_SIZE + physIdx * EVENT_SIZE;
    f.seek(offset);
    bool ok = (f.read((uint8_t*)&out, EVENT_SIZE) == EVENT_SIZE);
    f.close();
    return ok;
}

// -------------------------------------------------------------------------
// Initialize disk file with empty header + pre-allocated slots
// -------------------------------------------------------------------------
void EventLog::initDiskFile() {
    File f = LittleFS.open(_filename, "w");
    if (!f) {
        Serial.println("[EventLog] Failed to create disk file");
        _fsAvailable = false;
        return;
    }

    EventFileHeader hdr = {};
    hdr.magic = EVENT_FILE_MAGIC;
    hdr.capacity = DISK_CAPACITY;
    hdr.count = 0;
    hdr.head = 0;
    hdr.sequence = 0;
    f.write((const uint8_t*)&hdr, HEADER_SIZE);

    // Pre-allocate file — write zeros for all slots
    LogEvent empty = {};
    for (size_t i = 0; i < DISK_CAPACITY; i++) {
        f.write((const uint8_t*)&empty, EVENT_SIZE);
    }
    f.close();

    _diskHead = 0;
    _diskCount = 0;
    _diskSequence = 0;

    DBG("EventLog", "Disk file created: %u slots (%u bytes)", DISK_CAPACITY, HEADER_SIZE + DISK_CAPACITY * EVENT_SIZE);
}

// -------------------------------------------------------------------------
// Update disk file header
// -------------------------------------------------------------------------
void EventLog::updateDiskHeader() {
    File f = LittleFS.open(_filename, "r+");
    if (!f) return;

    EventFileHeader hdr;
    hdr.magic = EVENT_FILE_MAGIC;
    hdr.capacity = DISK_CAPACITY;
    hdr.count = _diskCount;
    hdr.head = _diskHead;
    hdr.sequence = _diskSequence;

    f.seek(0);
    f.write((const uint8_t*)&hdr, HEADER_SIZE);
    f.close();
}

// -------------------------------------------------------------------------
// Load disk state on boot
// -------------------------------------------------------------------------
void EventLog::loadFromDisk() {
    if (!LittleFS.exists(_filename)) {
        DBG("EventLog", "No disk file — starting fresh");
        return;
    }

    File f = LittleFS.open(_filename, "r");
    if (!f) return;

    // Read and validate header
    EventFileHeader hdr;
    if (f.read((uint8_t*)&hdr, HEADER_SIZE) != HEADER_SIZE) {
        f.close();
        DBG("EventLog", "Disk file header read failed — reinitializing");
        LittleFS.remove(_filename);
        return;
    }

    if (hdr.magic != EVENT_FILE_MAGIC || hdr.capacity != DISK_CAPACITY) {
        f.close();
        DBG("EventLog", "Disk file incompatible (magic=0x%08X cap=%u) — reinitializing", hdr.magic, hdr.capacity);
        LittleFS.remove(_filename);
        return;
    }

    _diskHead = hdr.head;
    _diskCount = (hdr.count > DISK_CAPACITY) ? DISK_CAPACITY : hdr.count;
    _diskSequence = hdr.sequence;
    f.close();

    // Load last N events into RAM buffer for quick access
    size_t loadCount = (_diskCount < _ramCapacity) ? _diskCount : _ramCapacity;
    size_t startIdx = (_diskCount > loadCount) ? _diskCount - loadCount : 0;

    for (size_t i = 0; i < loadCount; i++) {
        LogEvent evt;
        if (readEventFromDisk(startIdx + i, evt)) {
            _buffer[i] = evt;
        }
    }
    _head = 0;
    _count = loadCount;
    _lastFlushedSeq = _ramSequence; // don't re-flush loaded events

    DBG("EventLog", "Loaded %u disk events (showing last %u in RAM)", _diskCount, loadCount);
}

// -------------------------------------------------------------------------
// Clear all events
// -------------------------------------------------------------------------
void EventLog::clear() {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    _head = 0;
    _count = 0;
    _dirty = false;
    _ramSequence = 0;
    _lastFlushedSeq = 0;
    xSemaphoreGive(_mutex);

    LittleFS.remove(_filename);
    _diskHead = 0;
    _diskCount = 0;
    _diskSequence = 0;

    DBG("EventLog", "Cleared (RAM + disk)");
}

// -------------------------------------------------------------------------
// Paginated JSON output — reads from disk, newest first
// typeFilter: -1 = all, 0-5 = specific EventType
// -------------------------------------------------------------------------
void EventLog::getEventsJSON(JsonDocument& doc, uint32_t offset, uint32_t limit, int8_t typeFilter) {
    JsonObject root = doc.to<JsonObject>();
    root["total"] = _diskCount;
    root["capacity"] = DISK_CAPACITY;
    root["offset"] = offset;

    JsonArray arr = root["events"].to<JsonArray>();

    if (!_fsAvailable || _diskCount == 0) {
        root["count"] = 0;
        return;
    }

    // Read from disk, newest first
    uint32_t added = 0;
    uint32_t skipped = 0;

    for (uint32_t i = 0; i < _diskCount && added < limit; i++) {
        // newest first: read from end
        uint32_t logicalIdx = _diskCount - 1 - i;
        LogEvent evt;
        if (!readEventFromDisk(logicalIdx, evt)) continue;

        // Type filter
        if (typeFilter >= 0 && evt.type != (uint8_t)typeFilter) continue;

        // Offset (skip first N matching events)
        if (skipped < offset) {
            skipped++;
            continue;
        }

        JsonObject obj = arr.add<JsonObject>();
        obj["ts"] = evt.timestamp;
        obj["type"] = evt.type;
        obj["dist"] = evt.distance;
        obj["en"] = evt.energy;
        obj["msg"] = (const char*)evt.message;
        added++;
    }

    root["count"] = added;
}
