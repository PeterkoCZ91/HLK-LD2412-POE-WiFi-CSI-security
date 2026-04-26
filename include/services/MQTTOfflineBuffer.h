#ifndef MQTT_OFFLINE_BUFFER_H
#define MQTT_OFFLINE_BUFFER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <functional>

// One buffered MQTT message
struct BufferedMsg {
    uint32_t timestamp;     // epoch or uptime-s
    uint8_t  retained;      // 0 or 1
    uint8_t  _pad[3];
    char     topic[80];
    char     payload[200];
};  // 288 bytes

struct MQTTBufHeader {
    uint32_t magic;
    uint32_t capacity;
    uint32_t count;
    uint32_t head;
};  // 16 bytes

static constexpr uint32_t MQTT_BUF_MAGIC    = 0x4D510001;
// 200 slots × 288 B = ~57.6 KB on LittleFS (well within 16 MB partition).
// Sized for ~5 min outage at typical state-change publish rate so HA timeline
// stays continuous; pre-fix 50 dropped most messages on long ETH/MQTT gaps.
static constexpr size_t   MQTT_BUF_CAPACITY = 200;
static constexpr size_t   MQTT_BUF_HDR_SIZE = sizeof(MQTTBufHeader);
static constexpr size_t   MQTT_BUF_MSG_SIZE = sizeof(BufferedMsg);

using ReplayFn = std::function<bool(const char* topic, const char* payload, bool retained)>;

class MQTTOfflineBuffer {
public:
    MQTTOfflineBuffer() = default;

    // Call after LittleFS is mounted
    void begin();

    // Store a message when MQTT is offline
    // Returns false if topic/payload too long or buffer unavailable
    bool store(const char* topic, const char* payload, bool retained);

    // Replay all buffered messages via publishFn; clears buffer on success
    // publishFn should return true on success; stops on first failure
    uint16_t replay(ReplayFn publishFn);

    uint32_t count() const { return _count; }
    bool     available() const { return _fsAvailable; }
    void     clear();

private:
    bool initFile();
    bool updateHeader();
    bool readMsg(uint32_t index, BufferedMsg& out);

    bool     _fsAvailable = false;
    uint32_t _head  = 0;
    uint32_t _count = 0;

    const char* _filename = "/mqtt_buf.bin";
};

#endif
