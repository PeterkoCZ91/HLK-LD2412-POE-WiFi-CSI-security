// Native unit tests for notification cooldown keying.
// Run: pio test -e native -f test_notification_cooldown
//
// The defect this pins: the cooldown used to be keyed by NotificationType
// alone, but several unrelated producers share a type. Three of them raise
// TAMPER_ALERT — the radar tamper, the CSI tamper and anti-masking — so one
// false CSI tamper silenced a GENUINE radar tamper for the whole cooldown.
// Four more share HEALTH_WARNING.
//
// This has bitten the project before. SecurityMonitor.cpp carries a "FIX #18"
// comment where a zone alert was moved off TAMPER_ALERT for exactly this
// reason — which did not fix the collision, it moved it into HEALTH_WARNING.
// Keying by producer fixes the class instead of relocating it.
#include <unity.h>
#include <cstdio>
#include "services/NotificationCooldownPolicy.h"

void setUp() {}
void tearDown() {}

static const uint32_t FIVE_MIN = 300000;

void test_first_alert_of_a_producer_always_passes() {
    NotificationCooldownPolicy p;
    TEST_ASSERT_TRUE(p.allow(0, NotificationType::TAMPER_ALERT, AlertSource::RADAR_TAMPER));
}

void test_same_producer_is_rate_limited() {
    NotificationCooldownPolicy p;
    TEST_ASSERT_TRUE(p.allow(1000, NotificationType::TAMPER_ALERT, AlertSource::RADAR_TAMPER));
    p.stamp(1000, NotificationType::TAMPER_ALERT, AlertSource::RADAR_TAMPER);
    TEST_ASSERT_FALSE(p.allow(1000 + FIVE_MIN - 1, NotificationType::TAMPER_ALERT, AlertSource::RADAR_TAMPER));
    TEST_ASSERT_TRUE(p.allow(1000 + FIVE_MIN, NotificationType::TAMPER_ALERT, AlertSource::RADAR_TAMPER));
}

void test_a_false_csi_tamper_does_not_silence_a_real_radar_tamper() {
    // THE REGRESSION TEST. This is the field scenario: the CSI side cries wolf,
    // and seconds later somebody actually covers the radar.
    NotificationCooldownPolicy p;
    p.stamp(1000, NotificationType::TAMPER_ALERT, AlertSource::CSI_TAMPER);
    TEST_ASSERT_TRUE(p.allow(2000, NotificationType::TAMPER_ALERT, AlertSource::RADAR_TAMPER));
    TEST_ASSERT_TRUE(p.allow(2000, NotificationType::TAMPER_ALERT, AlertSource::ANTI_MASK));
    // ...while the CSI side itself stays muted, which is the point of a cooldown.
    TEST_ASSERT_FALSE(p.allow(2000, NotificationType::TAMPER_ALERT, AlertSource::CSI_TAMPER));
}

void test_health_warning_producers_are_independent() {
    // Four producers share HEALTH_WARNING; a chatty one must not mask the others.
    NotificationCooldownPolicy p;
    p.stamp(1000, NotificationType::HEALTH_WARNING, AlertSource::DISARM_REMINDER);
    TEST_ASSERT_FALSE(p.allow(2000, NotificationType::HEALTH_WARNING, AlertSource::DISARM_REMINDER));
    TEST_ASSERT_TRUE(p.allow(2000, NotificationType::HEALTH_WARNING, AlertSource::LOW_MEMORY));
    TEST_ASSERT_TRUE(p.allow(2000, NotificationType::HEALTH_WARNING, AlertSource::CROSSMODAL));
    TEST_ASSERT_TRUE(p.allow(2000, NotificationType::HEALTH_WARNING, AlertSource::SENSOR_SILENT));
}

void test_generic_source_still_keys_by_type() {
    // Producers that do not name themselves keep the old per-type behaviour,
    // so nothing that was rate-limited before becomes unlimited now.
    NotificationCooldownPolicy p;
    p.stamp(1000, NotificationType::SYSTEM_ERROR, AlertSource::GENERIC);
    TEST_ASSERT_FALSE(p.allow(2000, NotificationType::SYSTEM_ERROR, AlertSource::GENERIC));
    TEST_ASSERT_TRUE(p.allow(2000, NotificationType::HEALTH_WARNING, AlertSource::GENERIC));
}

void test_named_source_does_not_collide_with_its_generic_type() {
    NotificationCooldownPolicy p;
    p.stamp(1000, NotificationType::TAMPER_ALERT, AlertSource::GENERIC);
    TEST_ASSERT_TRUE(p.allow(2000, NotificationType::TAMPER_ALERT, AlertSource::CSI_TAMPER));
}

void test_wifi_anomaly_keeps_its_two_hour_cooldown() {
    NotificationCooldownPolicy p;
    p.stamp(1000, NotificationType::WIFI_ANOMALY, AlertSource::GENERIC);
    TEST_ASSERT_FALSE(p.allow(1000 + FIVE_MIN, NotificationType::WIFI_ANOMALY, AlertSource::GENERIC));
    TEST_ASSERT_FALSE(p.allow(1000 + 7200000 - 1, NotificationType::WIFI_ANOMALY, AlertSource::GENERIC));
    TEST_ASSERT_TRUE(p.allow(1000 + 7200000, NotificationType::WIFI_ANOMALY, AlertSource::GENERIC));
}

void test_configurable_default_cooldown() {
    NotificationCooldownPolicy p;
    p.defaultCooldownMs = 60000;
    p.stamp(0, NotificationType::TAMPER_ALERT, AlertSource::CSI_TAMPER);
    TEST_ASSERT_FALSE(p.allow(59999, NotificationType::TAMPER_ALERT, AlertSource::CSI_TAMPER));
    TEST_ASSERT_TRUE(p.allow(60000, NotificationType::TAMPER_ALERT, AlertSource::CSI_TAMPER));
}

void test_stamp_at_time_zero_is_recorded() {
    // millis() is genuinely 0 for the first millisecond after boot; a sentinel
    // of 0 for "never sent" must not resurrect a just-sent alert.
    NotificationCooldownPolicy p;
    p.stamp(0, NotificationType::TAMPER_ALERT, AlertSource::CSI_TAMPER);
    TEST_ASSERT_FALSE(p.allow(1000, NotificationType::TAMPER_ALERT, AlertSource::CSI_TAMPER));
}

void test_millis_rollover_is_survived() {
    NotificationCooldownPolicy p;
    const uint32_t nearMax = 0xFFFFF000u;
    p.stamp(nearMax, NotificationType::TAMPER_ALERT, AlertSource::RADAR_TAMPER);
    TEST_ASSERT_FALSE(p.allow(nearMax + 1000, NotificationType::TAMPER_ALERT, AlertSource::RADAR_TAMPER));
    TEST_ASSERT_TRUE(p.allow(nearMax + FIVE_MIN, NotificationType::TAMPER_ALERT, AlertSource::RADAR_TAMPER));
}

void test_reset_clears_every_slot() {
    NotificationCooldownPolicy p;
    p.stamp(1000, NotificationType::TAMPER_ALERT, AlertSource::CSI_TAMPER);
    p.stamp(1000, NotificationType::HEALTH_WARNING, AlertSource::LOW_MEMORY);
    p.reset();
    TEST_ASSERT_TRUE(p.allow(1100, NotificationType::TAMPER_ALERT, AlertSource::CSI_TAMPER));
    TEST_ASSERT_TRUE(p.allow(1100, NotificationType::HEALTH_WARNING, AlertSource::LOW_MEMORY));
}

void test_every_source_has_its_own_slot() {
    // Guards the slot arithmetic: stamping one producer must never mute another.
    NotificationCooldownPolicy p;
    for (uint8_t s = 0; s < (uint8_t)AlertSource::COUNT; s++) {
        p.reset();
        p.stamp(1000, NotificationType::HEALTH_WARNING, (AlertSource)s);
        for (uint8_t other = 0; other < (uint8_t)AlertSource::COUNT; other++) {
            if (other == s) continue;
            char msg[64];
            snprintf(msg, sizeof(msg), "source %u muted source %u", s, other);
            TEST_ASSERT_TRUE_MESSAGE(
                p.allow(2000, NotificationType::HEALTH_WARNING, (AlertSource)other), msg);
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_alert_of_a_producer_always_passes);
    RUN_TEST(test_same_producer_is_rate_limited);
    RUN_TEST(test_a_false_csi_tamper_does_not_silence_a_real_radar_tamper);
    RUN_TEST(test_health_warning_producers_are_independent);
    RUN_TEST(test_generic_source_still_keys_by_type);
    RUN_TEST(test_named_source_does_not_collide_with_its_generic_type);
    RUN_TEST(test_wifi_anomaly_keeps_its_two_hour_cooldown);
    RUN_TEST(test_configurable_default_cooldown);
    RUN_TEST(test_stamp_at_time_zero_is_recorded);
    RUN_TEST(test_millis_rollover_is_survived);
    RUN_TEST(test_reset_clears_every_slot);
    RUN_TEST(test_every_source_has_its_own_slot);
    return UNITY_END();
}
