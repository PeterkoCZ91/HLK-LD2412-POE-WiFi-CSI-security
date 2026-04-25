#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <freertos/semphr.h>

enum EventType {
    EVT_SYSTEM = 0,
    EVT_PRESENCE = 1,
    EVT_TAMPER = 2,
    EVT_WIFI = 3,        // kept for compat (ETH anomaly on PoE)
    EVT_HEARTBEAT = 4,
    EVT_SECURITY = 5
};

struct LogEvent {
    uint32_t timestamp;      // epoch or uptime seconds
    uint8_t type;            // EventType
    uint16_t distance;       // cm
    uint8_t energy;          // max energy
    char message[48];        // Short description
};  // 56 bytes

// Disk file header for ring buffer persistence
struct EventFileHeader {
    uint32_t magic;          // 0xEV120001
    uint32_t capacity;       // max events on disk
    uint32_t count;          // current event count
    uint32_t head;           // index of oldest event
    uint32_t sequence;       // monotonic counter
};  // 20 bytes

static constexpr uint32_t EVENT_FILE_MAGIC = 0xEE120001;
static constexpr size_t   DISK_CAPACITY    = 500;
static constexpr size_t   RAM_CAPACITY     = 50;
static constexpr size_t   HEADER_SIZE      = sizeof(EventFileHeader);
static constexpr size_t   EVENT_SIZE       = sizeof(LogEvent);

class EventLog {
public:
    EventLog(size_t ramCapacity = RAM_CAPACITY);
    ~EventLog();

    // begin() expects LittleFS already mounted externally
    void begin(bool fsAvailable = true);
    void addEvent(uint8_t type, uint16_t dist, uint8_t energy, const char* msg);
    void flush();
    void flushNow();
    void clear();

    // JSON output — paginated from disk, newest first
    void getEventsJSON(JsonDocument& doc, uint32_t offset = 0, uint32_t limit = 50, int8_t typeFilter = -1);

    // Stats
    size_t getDiskCount() const { return _diskCount; }
    size_t getDiskCapacity() const { return DISK_CAPACITY; }

private:
    void loadFromDisk();
    void flushToDisk();
    bool writeEventToDisk(const LogEvent& evt);
    bool readEventFromDisk(size_t index, LogEvent& out);
    void initDiskFile();
    void updateDiskHeader();

    // RAM ring buffer (write-back cache)
    SemaphoreHandle_t _mutex = nullptr;
    LogEvent* _buffer;
    size_t _ramCapacity;
    size_t _head;
    size_t _count;

    // Disk ring state
    uint32_t _diskHead = 0;
    uint32_t _diskCount = 0;
    uint32_t _diskSequence = 0;

    bool _dirty = false;
    bool _fsAvailable = false;
    uint32_t _ramSequence = 0;
    uint32_t _lastFlushedSeq = 0;
    unsigned long _lastFlush = 0;
    const char* _filename = "/events.bin";
};

#endif
