// Native unit tests for CSI-side tamper detection (#5).
// Run: pio test -e native -f test_csi_tamper
#include <unity.h>
#include <cstring>
#include "services/CsiTamperDetector.h"

void setUp() {}
void tearDown() {}

static CsiTamperInputs healthy(uint32_t now) {
    CsiTamperInputs in;
    in.csiActive = true; in.ethUp = true; in.packetRate = 100.0f;
    in.variance = 0.002f; in.nowMs = now;
    return in;
}

void test_healthy_no_tamper() {
    CsiTamperDetector d;
    uint32_t f = 0;
    for (uint32_t t = 0; t <= 300000; t += 10000) {
        CsiTamperInputs in = healthy(t);
        in.variance = 0.002f + (float)t * 1e-9f;  // varies slightly -> not frozen
        f = d.update(in);
    }
    TEST_ASSERT_EQUAL_UINT32(CSI_TAMPER_NONE, f);
}

void test_no_packets_flagged_after_grace() {
    CsiTamperDetector d;
    // packets stop at t=0 with eth up
    TEST_ASSERT_EQUAL_UINT32(CSI_TAMPER_NONE, d.update({true, true, 0.0f, 0.002f, 0}));
    // still within grace at 20s
    TEST_ASSERT_FALSE(d.update({true, true, 0.0f, 0.0021f, 20000}) & CSI_TAMPER_NO_PACKETS);
    // past 30s grace
    TEST_ASSERT_TRUE(d.update({true, true, 0.0f, 0.0022f, 31000}) & CSI_TAMPER_NO_PACKETS);
}

void test_no_packets_not_flagged_when_eth_down() {
    CsiTamperDetector d;
    d.update({true, false, 0.0f, 0.002f, 0});
    // eth down -> collapse is a network outage, not tamper
    TEST_ASSERT_FALSE(d.update({true, false, 0.0f, 0.0021f, 60000}) & CSI_TAMPER_NO_PACKETS);
}

void test_packets_recover_clears_timer() {
    CsiTamperDetector d;
    d.update({true, true, 0.0f, 0.002f, 0});
    d.update({true, true, 100.0f, 0.0021f, 20000});   // recovered
    // new collapse timer restarts -> not yet flagged 20s later
    TEST_ASSERT_FALSE(d.update({true, true, 0.0f, 0.0022f, 40000}) & CSI_TAMPER_NO_PACKETS);
}

void test_frozen_variance_flagged() {
    CsiTamperDetector d;
    d.update({true, true, 100.0f, 0.00500f, 0});      // establish baseline
    TEST_ASSERT_FALSE(d.update({true, true, 100.0f, 0.00500f, 60000}) & CSI_TAMPER_FROZEN);
    TEST_ASSERT_TRUE(d.update({true, true, 100.0f, 0.00500f, 130000}) & CSI_TAMPER_FROZEN);
}

void test_variance_change_clears_frozen() {
    CsiTamperDetector d;
    d.update({true, true, 100.0f, 0.005f, 0});
    d.update({true, true, 100.0f, 0.006f, 130000});   // changed -> resets
    TEST_ASSERT_FALSE(d.update({true, true, 100.0f, 0.006f, 200000}) & CSI_TAMPER_FROZEN);
}

void test_csi_inactive_resets() {
    CsiTamperDetector d;
    d.update({true, true, 0.0f, 0.002f, 0});
    // CSI turned off -> no tamper, timers reset
    TEST_ASSERT_EQUAL_UINT32(CSI_TAMPER_NONE, d.update({false, true, 0.0f, 0.002f, 60000}));
    // back on, timer restarts fresh
    TEST_ASSERT_FALSE(d.update({true, true, 0.0f, 0.002f, 61000}) & CSI_TAMPER_NO_PACKETS);
}

void test_render_tamper_reasons() {
    char buf[128];
    int n = renderCsiTamper(CSI_TAMPER_NO_PACKETS | CSI_TAMPER_FROZEN, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "packets stopped"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "frozen"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_healthy_no_tamper);
    RUN_TEST(test_no_packets_flagged_after_grace);
    RUN_TEST(test_no_packets_not_flagged_when_eth_down);
    RUN_TEST(test_packets_recover_clears_timer);
    RUN_TEST(test_frozen_variance_flagged);
    RUN_TEST(test_variance_change_clears_frozen);
    RUN_TEST(test_csi_inactive_resets);
    RUN_TEST(test_render_tamper_reasons);
    return UNITY_END();
}
