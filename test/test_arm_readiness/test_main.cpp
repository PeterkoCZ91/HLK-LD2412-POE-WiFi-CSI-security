// Native unit tests for the pre-arm health self-test (#9).
// Run: pio test -e native -f test_arm_readiness
#include <unity.h>
#include <cstring>
#include "services/ArmReadiness.h"

void setUp() {}
void tearDown() {}

// A fully healthy node: radar up, CSI active with packets + valid fresh model,
// clock synced, MQTT connected.
static ArmHealthInputs healthy() {
    ArmHealthInputs in;
    in.csiCompiled = true; in.csiEnabled = true; in.csiActive = true;
    in.csiPacketRate = 100.0f;
    in.radarOk = true;
    in.modelValid = true; in.modelAgeSec = 3600;
    in.timeValid = true;
    in.mqttWanted = true; in.mqttConnected = true;
    return in;
}

void test_healthy_no_warnings() {
    TEST_ASSERT_EQUAL_UINT32(ARM_WARN_NONE, computeArmWarnings(healthy()));
}

void test_radar_down_flagged() {
    ArmHealthInputs in = healthy(); in.radarOk = false;
    TEST_ASSERT_TRUE(computeArmWarnings(in) & ARM_WARN_RADAR_DOWN);
}

void test_csi_no_packets_flagged() {
    ArmHealthInputs in = healthy(); in.csiPacketRate = 0.0f;
    TEST_ASSERT_TRUE(computeArmWarnings(in) & ARM_WARN_CSI_NO_PACKETS);
}

void test_csi_inactive_flagged() {
    ArmHealthInputs in = healthy(); in.csiActive = false;
    TEST_ASSERT_TRUE(computeArmWarnings(in) & ARM_WARN_CSI_NO_PACKETS);
}

void test_model_missing_flagged() {
    ArmHealthInputs in = healthy(); in.modelValid = false;
    uint32_t w = computeArmWarnings(in);
    TEST_ASSERT_TRUE(w & ARM_WARN_MODEL_MISSING);
    TEST_ASSERT_FALSE(w & ARM_WARN_MODEL_STALE);  // missing takes precedence over stale
}

void test_model_stale_flagged() {
    ArmHealthInputs in = healthy(); in.modelAgeSec = ARM_MODEL_STALE_SEC + 1;
    TEST_ASSERT_TRUE(computeArmWarnings(in) & ARM_WARN_MODEL_STALE);
}

void test_clock_invalid_flagged() {
    ArmHealthInputs in = healthy(); in.timeValid = false;
    TEST_ASSERT_TRUE(computeArmWarnings(in) & ARM_WARN_CLOCK_INVALID);
}

void test_mqtt_down_only_when_wanted() {
    ArmHealthInputs in = healthy(); in.mqttConnected = false;
    TEST_ASSERT_TRUE(computeArmWarnings(in) & ARM_WARN_MQTT_DOWN);
    in.mqttWanted = false;  // MQTT off in config -> not a warning
    TEST_ASSERT_FALSE(computeArmWarnings(in) & ARM_WARN_MQTT_DOWN);
}

void test_csi_checks_skipped_when_csi_disabled() {
    // CSI disabled at runtime: no CSI/model warnings even with no packets/model.
    ArmHealthInputs in = healthy();
    in.csiEnabled = false; in.csiActive = false; in.csiPacketRate = 0.0f; in.modelValid = false;
    uint32_t w = computeArmWarnings(in);
    TEST_ASSERT_FALSE(w & ARM_WARN_CSI_NO_PACKETS);
    TEST_ASSERT_FALSE(w & ARM_WARN_MODEL_MISSING);
}

void test_render_lists_reasons() {
    ArmHealthInputs in = healthy(); in.radarOk = false; in.timeValid = false;
    char buf[128];
    int n = renderArmWarnings(computeArmWarnings(in), buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "radar offline"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "clock not set"));
    TEST_ASSERT_NOT_NULL(strstr(buf, ", "));  // comma-separated
}

void test_render_empty_when_healthy() {
    char buf[64];
    int n = renderArmWarnings(computeArmWarnings(healthy()), buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_STRING("", buf);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_healthy_no_warnings);
    RUN_TEST(test_radar_down_flagged);
    RUN_TEST(test_csi_no_packets_flagged);
    RUN_TEST(test_csi_inactive_flagged);
    RUN_TEST(test_model_missing_flagged);
    RUN_TEST(test_model_stale_flagged);
    RUN_TEST(test_clock_invalid_flagged);
    RUN_TEST(test_mqtt_down_only_when_wanted);
    RUN_TEST(test_csi_checks_skipped_when_csi_disabled);
    RUN_TEST(test_render_lists_reasons);
    RUN_TEST(test_render_empty_when_healthy);
    return UNITY_END();
}
