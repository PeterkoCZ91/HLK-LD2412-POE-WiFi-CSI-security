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

#endif
