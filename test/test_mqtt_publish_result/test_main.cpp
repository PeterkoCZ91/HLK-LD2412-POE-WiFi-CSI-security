#include <unity.h>
#include "services/MqttPublishResult.h"

void setUp() {}
void tearDown() {}

void test_publish_result_consume_decision() {
    TEST_ASSERT_TRUE(mqttPublishResultConsumes(PublishResult::PUBLISHED));
    TEST_ASSERT_TRUE(mqttPublishResultConsumes(PublishResult::QUEUED_OFFLINE));
    TEST_ASSERT_FALSE(mqttPublishResultConsumes(PublishResult::FAILED));
}

void test_event_id_dedup_only_applies_to_nonzero_ids() {
    TEST_ASSERT_TRUE(mqttBufferedEventIdsMatch(0x123456789ULL, 0x123456789ULL));
    TEST_ASSERT_FALSE(mqttBufferedEventIdsMatch(0x123456789ULL, 0x123456788ULL));
    TEST_ASSERT_FALSE(mqttBufferedEventIdsMatch(0, 0));
}

void test_offline_buffer_persists_events_only() {
    TEST_ASSERT_FALSE(mqttOfflineBufferShouldStore(0));
    TEST_ASSERT_TRUE(mqttOfflineBufferShouldStore(1));
    TEST_ASSERT_TRUE(mqttOfflineBufferShouldStore(0x123456789ULL));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_publish_result_consume_decision);
    RUN_TEST(test_event_id_dedup_only_applies_to_nonzero_ids);
    RUN_TEST(test_offline_buffer_persists_events_only);
    return UNITY_END();
}
