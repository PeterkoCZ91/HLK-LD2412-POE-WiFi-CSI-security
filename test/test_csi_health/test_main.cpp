// Native unit tests for CSI health-reason flags (P1.4, navrh 17.7).
// Pure — no Arduino. Run: pio test -e native -f test_csi_health
#include <unity.h>
#include "services/CsiHealthReasons.h"

void setUp() {}
void tearDown() {}

// A fully healthy snapshot — clock synced, model valid, radar up, packets flowing.
static CsiHealthInputs healthy() {
    CsiHealthInputs in;
    in.csiActive = true; in.htLtfSeen = true;
    in.packetRate = 80.0f; in.packetRateFloorPps = 5.0f; in.packetRateUnstable = false;
    in.wifiRoamedRecently = false;
    in.modelValid = true; in.modelStale = false;
    in.learningContaminated = false;
    in.radarAvailable = true;
    in.mqttExpected = true; in.mqttConnected = true;
    in.clockValid = true;
    return in;
}

void test_healthy_has_no_flags_and_full_score() {
    uint16_t f = csiHealthReasons(healthy());
    TEST_ASSERT_EQUAL_UINT16(CSI_HEALTH_OK, f);
    TEST_ASSERT_EQUAL_UINT8(100, csiHealthScore(f));
}

void test_no_ht_ltf_only_when_csi_active() {
    CsiHealthInputs in = healthy(); in.htLtfSeen = false;
    TEST_ASSERT_TRUE(csiHealthReasons(in) & CSI_HEALTH_NO_HT_LTF);
    // CSI inactive → no-HT-LTF is not a fault
    in.csiActive = false;
    TEST_ASSERT_FALSE(csiHealthReasons(in) & CSI_HEALTH_NO_HT_LTF);
}

void test_packet_rate_low() {
    CsiHealthInputs in = healthy(); in.packetRate = 2.0f;  // below 5 floor
    TEST_ASSERT_TRUE(csiHealthReasons(in) & CSI_HEALTH_PACKET_RATE_LOW);
}

void test_model_missing_suppresses_stale() {
    // when the model is missing, we report MISSING, not STALE
    CsiHealthInputs in = healthy(); in.modelValid = false; in.modelStale = true;
    uint16_t f = csiHealthReasons(in);
    TEST_ASSERT_TRUE(f & CSI_HEALTH_MODEL_MISSING);
    TEST_ASSERT_FALSE(f & CSI_HEALTH_MODEL_STALE);
}

void test_stale_only_when_clock_valid() {
    CsiHealthInputs in = healthy(); in.modelStale = true;
    TEST_ASSERT_TRUE(csiHealthReasons(in) & CSI_HEALTH_MODEL_STALE);
    // clock invalid → staleness untrusted, only CLOCK_INVALID reported
    in.clockValid = false;
    uint16_t f = csiHealthReasons(in);
    TEST_ASSERT_FALSE(f & CSI_HEALTH_MODEL_STALE);
    TEST_ASSERT_TRUE(f & CSI_HEALTH_CLOCK_INVALID);
}

void test_mqtt_disconnected_only_when_expected() {
    CsiHealthInputs in = healthy(); in.mqttConnected = false;
    TEST_ASSERT_TRUE(csiHealthReasons(in) & CSI_HEALTH_MQTT_DISCONNECTED);
    // MQTT not configured → its disconnection is not a fault
    in.mqttExpected = false;
    TEST_ASSERT_FALSE(csiHealthReasons(in) & CSI_HEALTH_MQTT_DISCONNECTED);
}

void test_radar_unavailable_flag() {
    CsiHealthInputs in = healthy(); in.radarAvailable = false;
    TEST_ASSERT_TRUE(csiHealthReasons(in) & CSI_HEALTH_RADAR_UNAVAILABLE);
}

void test_score_clamps_at_zero() {
    // pile on the heaviest faults — score must clamp, never go negative
    CsiHealthInputs in = healthy();
    in.htLtfSeen = false;        // -40
    in.packetRate = 0.0f;        // -30
    in.modelValid = false;       // -40
    in.radarAvailable = false;   // -30
    in.clockValid = false;       // -5
    uint16_t f = csiHealthReasons(in);
    TEST_ASSERT_EQUAL_UINT8(0, csiHealthScore(f));
}

void test_score_partial_deduction() {
    CsiHealthInputs in = healthy();
    in.wifiRoamedRecently = true;   // -10
    in.packetRateUnstable = true;   // -15
    TEST_ASSERT_EQUAL_UINT8(75, csiHealthScore(csiHealthReasons(in)));
}

void test_flag_strings() {
    TEST_ASSERT_EQUAL_STRING("no_ht_ltf", csiHealthFlagStr(CSI_HEALTH_NO_HT_LTF));
    TEST_ASSERT_EQUAL_STRING("learning_contaminated", csiHealthFlagStr(CSI_HEALTH_LEARNING_CONTAMINATED));
    TEST_ASSERT_EQUAL_STRING("clock_invalid", csiHealthFlagStr(CSI_HEALTH_CLOCK_INVALID));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_healthy_has_no_flags_and_full_score);
    RUN_TEST(test_no_ht_ltf_only_when_csi_active);
    RUN_TEST(test_packet_rate_low);
    RUN_TEST(test_model_missing_suppresses_stale);
    RUN_TEST(test_stale_only_when_clock_valid);
    RUN_TEST(test_mqtt_disconnected_only_when_expected);
    RUN_TEST(test_radar_unavailable_flag);
    RUN_TEST(test_score_clamps_at_zero);
    RUN_TEST(test_score_partial_deduction);
    RUN_TEST(test_flag_strings);
    return UNITY_END();
}
