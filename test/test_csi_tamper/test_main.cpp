// Native unit tests for CSI-side tamper detection (#5).
// Run: pio test -e native -f test_csi_tamper
//
// Field history (z15 node, 2026-08-19..22): this detector produced 13 false
// "🛡️ CSI sensor tamper" alerts, every one of them either just after a boot or
// during the OOM crash-loop era, none while the node ran undisturbed. Two
// defects caused them and both are pinned below.
//
//   1. ALIASING. SecurityMonitor samples this detector once per 60 s
//      (INTERVAL_HEALTH_CHECK_MS), but it was fed getPacketRate() — an
//      instantaneous 1-second average. Two unlucky 1 s dips 60 s apart were
//      indistinguishable from 60 s of genuine blindness. The detector now
//      takes the monotonic packet COUNT and derives the interval average
//      itself, so "no packets" means no packets for the whole interval,
//      whatever cadence the caller happens to use.
//
//   2. BOOT TRANSIENT. At boot Ethernet is up long before the WiFi station
//      has associated and CSI frames start flowing, so a fresh node reported
//      itself sabotaged ~2-4 minutes in, once per boot. A startup grace now
//      covers the window until the first packet is seen.
//
// The threat model is unchanged and deliberately NOT gated on WiFi
// association: pulling or covering the AP disassociates the station, and that
// is exactly the attack this is here to catch. Transient disassociation is
// excused by duration, not by suppressing the check.
#include <unity.h>
#include <cstring>
#include "services/CsiTamperDetector.h"

void setUp() {}
void tearDown() {}

// Healthy sample: packets keep arriving, variance keeps moving.
static CsiTamperInputs healthy(uint32_t now, uint32_t packets) {
    CsiTamperInputs in;
    in.csiActive = true; in.ethUp = true; in.packetCount = packets;
    in.variance = 0.002f; in.nowMs = now;
    return in;
}

// A detector already past its startup grace with packets flowing, so tests
// can exercise steady-state behaviour without restating the warm-up.
static void warmUp(CsiTamperDetector& d, uint32_t& now, uint32_t& packets) {
    for (int i = 0; i < 3; i++) {
        now += 60000; packets += 6000;
        CsiTamperInputs in = healthy(now, packets);
        in.variance = 0.002f + (float)i * 1e-5f;
        d.update(in);
    }
}

void test_healthy_no_tamper() {
    CsiTamperDetector d;
    uint32_t f = 0, packets = 0;
    for (uint32_t t = 0; t <= 600000; t += 60000) {
        CsiTamperInputs in = healthy(t, packets);
        packets += 6000;                              // ~100 pps
        in.variance = 0.002f + (float)t * 1e-9f;      // varies -> not frozen
        f = d.update(in);
    }
    TEST_ASSERT_EQUAL_UINT32(CSI_TAMPER_NONE, f);
}

// --- the aliasing defect ------------------------------------------------

void test_instantaneous_dips_are_not_tamper() {
    // THE REGRESSION TEST. Packet flow is healthy overall (6000 packets per
    // 60 s interval) and the old detector saw pps<1 at both probe instants
    // purely because it sampled a 1-second window. Interval averaging must
    // read this as healthy.
    CsiTamperDetector d;
    uint32_t now = 0, packets = 0;
    warmUp(d, now, packets);
    now += 60000; packets += 6000;
    d.update(healthy(now, packets));
    now += 60000; packets += 6000;
    uint32_t f = d.update(healthy(now, packets));
    TEST_ASSERT_FALSE(f & CSI_TAMPER_NO_PACKETS);
}

void test_no_packets_across_whole_interval_is_tamper() {
    // The count does not advance at all between probes -> genuinely blind for
    // the full 60 s, well past the 30 s grace.
    CsiTamperDetector d;
    uint32_t now = 0, packets = 0;
    warmUp(d, now, packets);
    now += 60000;                                    // count frozen
    d.update(healthy(now, packets));
    now += 60000;
    uint32_t f = d.update(healthy(now, packets));
    TEST_ASSERT_TRUE(f & CSI_TAMPER_NO_PACKETS);
}

void test_trickle_below_min_pps_still_tamper() {
    // Covering an AP need not stop every frame. A handful of packets per
    // minute is still blind, and must not reset the clock the way a single
    // count change would.
    CsiTamperDetector d;
    uint32_t now = 0, packets = 0;
    warmUp(d, now, packets);
    uint32_t f = 0;
    for (int i = 0; i < 3; i++) {
        now += 60000; packets += 10;                 // 0.17 pps << 1.0
        f = d.update(healthy(now, packets));
    }
    TEST_ASSERT_TRUE(f & CSI_TAMPER_NO_PACKETS);
}

void test_recovery_clears_the_flag() {
    CsiTamperDetector d;
    uint32_t now = 0, packets = 0;
    warmUp(d, now, packets);
    now += 60000; d.update(healthy(now, packets));
    now += 60000;
    TEST_ASSERT_TRUE(d.update(healthy(now, packets)) & CSI_TAMPER_NO_PACKETS);
    now += 60000; packets += 6000;                   // flow returns
    TEST_ASSERT_FALSE(d.update(healthy(now, packets)) & CSI_TAMPER_NO_PACKETS);
}

void test_no_packets_not_flagged_when_eth_down() {
    // Everything is off the air, not just the sensor — a network outage, not
    // sabotage.
    CsiTamperDetector d;
    uint32_t now = 0, packets = 0;
    warmUp(d, now, packets);
    uint32_t f = 0;
    for (int i = 0; i < 3; i++) {
        now += 60000;
        CsiTamperInputs in = healthy(now, packets);
        in.ethUp = false;
        f = d.update(in);
    }
    TEST_ASSERT_FALSE(f & CSI_TAMPER_NO_PACKETS);
}

// --- the boot transient -------------------------------------------------

void test_boot_before_first_packet_is_not_tamper() {
    // THE REGRESSION TEST. ETH is up from the first second; the WiFi station
    // has not associated yet, so no CSI frame has ever arrived. The node is
    // starting up, not being sabotaged.
    CsiTamperDetector d;
    uint32_t f = 0;
    for (uint32_t t = 0; t <= 240000; t += 60000) {   // 4 min, the field case
        f = d.update(healthy(t, 0));
    }
    TEST_ASSERT_EQUAL_UINT32(CSI_TAMPER_NONE, f);
}

void test_startup_grace_is_bounded() {
    // The grace excuses a slow start, not a sensor that never works at all.
    CsiTamperDetector d;
    uint32_t f = 0;
    for (uint32_t t = 0; t <= 600000; t += 60000) {
        f = d.update(healthy(t, 0));
    }
    TEST_ASSERT_TRUE(f & CSI_TAMPER_NO_PACKETS);
}

void test_first_packet_ends_the_grace() {
    // Once frames have flowed the node is proven healthy, so a later collapse
    // is judged on the normal grace rather than the startup one.
    CsiTamperDetector d;
    uint32_t now = 0;
    d.update(healthy(now, 0));
    now += 60000;  d.update(healthy(now, 5000));      // first packets seen
    now += 60000;  d.update(healthy(now, 5000));      // flow stops dead
    now += 60000;
    uint32_t f = d.update(healthy(now, 5000));
    TEST_ASSERT_TRUE(f & CSI_TAMPER_NO_PACKETS);
}

// --- frozen variance (unchanged semantics) ------------------------------

void test_frozen_variance_flagged() {
    CsiTamperDetector d;
    uint32_t now = 0, packets = 0;
    warmUp(d, now, packets);
    now += 60000; packets += 6000;
    CsiTamperInputs in = healthy(now, packets);
    in.variance = 0.005f;
    d.update(in);                                     // establish baseline
    now += 60000; packets += 6000;
    in = healthy(now, packets); in.variance = 0.005f;
    TEST_ASSERT_FALSE(d.update(in) & CSI_TAMPER_FROZEN);
    now += 70000; packets += 7000;
    in = healthy(now, packets); in.variance = 0.005f;
    TEST_ASSERT_TRUE(d.update(in) & CSI_TAMPER_FROZEN);
}

void test_variance_change_clears_frozen() {
    CsiTamperDetector d;
    uint32_t now = 0, packets = 0;
    warmUp(d, now, packets);
    now += 60000; packets += 6000;
    CsiTamperInputs in = healthy(now, packets); in.variance = 0.005f;
    d.update(in);
    now += 130000; packets += 13000;
    in = healthy(now, packets); in.variance = 0.006f;  // changed -> resets
    d.update(in);
    now += 70000; packets += 7000;
    in = healthy(now, packets); in.variance = 0.006f;
    TEST_ASSERT_FALSE(d.update(in) & CSI_TAMPER_FROZEN);
}

void test_frozen_not_flagged_before_first_packet() {
    // Variance sits at exactly 0.0 until the first frame fills the window;
    // that is not a stuck capture, it is an empty one.
    CsiTamperDetector d;
    uint32_t f = 0;
    for (uint32_t t = 0; t <= 240000; t += 60000) {
        CsiTamperInputs in = healthy(t, 0);
        in.variance = 0.0f;
        f = d.update(in);
    }
    TEST_ASSERT_FALSE(f & CSI_TAMPER_FROZEN);
}

void test_csi_inactive_resets() {
    CsiTamperDetector d;
    uint32_t now = 0, packets = 0;
    warmUp(d, now, packets);
    now += 60000;
    CsiTamperInputs off = healthy(now, packets);
    off.csiActive = false;
    TEST_ASSERT_EQUAL_UINT32(CSI_TAMPER_NONE, d.update(off));
    // Back on: the startup grace applies again, so a cold restart of the
    // capture is not instantly sabotage.
    now += 60000;
    TEST_ASSERT_EQUAL_UINT32(CSI_TAMPER_NONE, d.update(healthy(now, packets)));
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
    RUN_TEST(test_instantaneous_dips_are_not_tamper);
    RUN_TEST(test_no_packets_across_whole_interval_is_tamper);
    RUN_TEST(test_trickle_below_min_pps_still_tamper);
    RUN_TEST(test_recovery_clears_the_flag);
    RUN_TEST(test_no_packets_not_flagged_when_eth_down);
    RUN_TEST(test_boot_before_first_packet_is_not_tamper);
    RUN_TEST(test_startup_grace_is_bounded);
    RUN_TEST(test_first_packet_ends_the_grace);
    RUN_TEST(test_frozen_variance_flagged);
    RUN_TEST(test_variance_change_clears_frozen);
    RUN_TEST(test_frozen_not_flagged_before_first_packet);
    RUN_TEST(test_csi_inactive_resets);
    RUN_TEST(test_render_tamper_reasons);
    return UNITY_END();
}
