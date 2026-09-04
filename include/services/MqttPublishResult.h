#ifndef MQTT_PUBLISH_RESULT_H
#define MQTT_PUBLISH_RESULT_H

#include <stdint.h>

enum class PublishResult : uint8_t {
    PUBLISHED,
    QUEUED_OFFLINE,
    FAILED
};

inline bool mqttPublishResultConsumes(PublishResult result) {
    return result != PublishResult::FAILED;
}

inline bool mqttBufferedEventIdsMatch(uint64_t storedId, uint64_t candidateId) {
    return candidateId != 0 && storedId == candidateId;
}

// Persistent offline storage is reserved for security/alarm events. Ordinary
// state and telemetry publishes can be frequent; writing each one to LittleFS
// while MQTT is down can block loopTask long enough to trip the Task WDT.
inline bool mqttOfflineBufferShouldStore(uint64_t eventId) {
    return eventId != 0;
}

#endif
