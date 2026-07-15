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

// ---- debounce (v5.3.1: oscillating flags must not spam the event ring) -----
void test_debounce_stable_state_logs_once() {
    CsiHealthDebounce d(3);
    TEST_ASSERT_FALSE(d.feed(CSI_HEALTH_OK));   // tick 1
    TEST_ASSERT_FALSE(d.feed(CSI_HEALTH_OK));   // tick 2
    TEST_ASSERT_TRUE (d.feed(CSI_HEALTH_OK));   // tick 3 — stable → log initial state
    // further identical ticks never re-log
    for (int i = 0; i < 10; i++) TEST_ASSERT_FALSE(d.feed(CSI_HEALTH_OK));
}

void test_debounce_oscillation_never_logs() {
    // the lab failure mode: packet_rate_low <-> +unstable flip every tick
    CsiHealthDebounce d(3);
    uint16_t a = CSI_HEALTH_PACKET_RATE_LOW;
    uint16_t b = CSI_HEALTH_PACKET_RATE_LOW | CSI_HEALTH_PACKET_RATE_UNSTABLE;
    for (int i = 0; i < 100; i++) {
        TEST_ASSERT_FALSE(d.feed(i % 2 ? a : b));
    }
}

void test_debounce_genuine_transition_logs_after_stability() {
    CsiHealthDebounce d(3);
    for (int i = 0; i < 3; i++) d.feed(CSI_HEALTH_OK);        // initial logged on 3rd
    TEST_ASSERT_EQUAL_UINT16(CSI_HEALTH_OK, d.reported());
    // real fault appears and STAYS
    uint16_t f = CSI_HEALTH_PACKET_RATE_LOW;
    TEST_ASSERT_FALSE(d.feed(f));   // 1
    TEST_ASSERT_FALSE(d.feed(f));   // 2
    TEST_ASSERT_TRUE (d.feed(f));   // 3 — stable fault → log
    TEST_ASSERT_EQUAL_UINT16(f, d.reported());
    // brief flicker back to OK (1 tick) then fault again → no log
    TEST_ASSERT_FALSE(d.feed(CSI_HEALTH_OK));
    TEST_ASSERT_FALSE(d.feed(f));
    TEST_ASSERT_FALSE(d.feed(f));
    TEST_ASSERT_FALSE(d.feed(f));   // f already reported → no re-log
    TEST_ASSERT_EQUAL_UINT16(f, d.reported());
}

void test_debounce_return_to_reported_state_never_relogs() {
    CsiHealthDebounce d(2);
    d.feed(CSI_HEALTH_OK); d.feed(CSI_HEALTH_OK);             // OK reported
    uint16_t f = CSI_HEALTH_RADAR_UNAVAILABLE;
    d.feed(f); d.feed(f);                                      // fault reported
    // back to OK and stable → logs the recovery exactly once
    TEST_ASSERT_FALSE(d.feed(CSI_HEALTH_OK));
    TEST_ASSERT_TRUE (d.feed(CSI_HEALTH_OK));
    for (int i = 0; i < 5; i++) TEST_ASSERT_FALSE(d.feed(CSI_HEALTH_OK));
}

// ---- ML vote trust gate (v5.4: lab node ml_probability saturated near 1.0
// at chronic packet_rate_low — DSER/turbulence features use packet-count time
// constants tuned for a much higher pps than this site sustains) -----------
void test_ml_vote_untrusted_below_packet_rate_floor() {
    TEST_ASSERT_FALSE(csiMlVoteTrusted(2.0f, 5.0f));
}

void test_ml_vote_trusted_above_packet_rate_floor() {
    TEST_ASSERT_TRUE(csiMlVoteTrusted(80.0f, 5.0f));
}

void test_ml_vote_trusted_at_floor_boundary() {
    TEST_ASSERT_TRUE(csiMlVoteTrusted(5.0f, 5.0f));
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
    RUN_TEST(test_debounce_stable_state_logs_once);
    RUN_TEST(test_debounce_oscillation_never_logs);
    RUN_TEST(test_debounce_genuine_transition_logs_after_stability);
    RUN_TEST(test_debounce_return_to_reported_state_never_relogs);
    RUN_TEST(test_ml_vote_untrusted_below_packet_rate_floor);
    RUN_TEST(test_ml_vote_trusted_above_packet_rate_floor);
    RUN_TEST(test_ml_vote_trusted_at_floor_boundary);
    return UNITY_END();
}
