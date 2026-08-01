// Native unit tests for cross-modal liveness (#1).
// Run: pio test -e native -f test_crossmodal_liveness
#include <unity.h>
#include <cstring>
#include "services/CrossModalLiveness.h"

void setUp() {}
void tearDown() {}

// Drive `ticks` frames at `stepMs`; radar always sees, CSI sees only if `csiOn`.
static uint32_t drive(CrossModalLiveness& d, bool radarOn, bool csiOn,
                      uint32_t startMs, uint32_t stepMs, uint32_t ticks) {
    uint32_t f = 0;
    for (uint32_t i = 0; i < ticks; i++) {
        CrossModalInputs in;
        in.radarSees = radarOn;
        in.csiSees = csiOn;
        in.bothLive = true;
        in.nowMs = startMs + i * stepMs;
        f = d.update(in);
    }
    return f;
}

void test_agreement_never_flags() {
    CrossModalLiveness d;  // defaults: 10min window, 8s lag, 60s active, 0.2 ratio
    uint32_t f = drive(d, true, true, 0, 1000, 700);
    TEST_ASSERT_EQUAL_UINT32(CROSSMODAL_NONE, f);
}

void test_radar_without_csi_flags_after_window() {
    CrossModalLiveness d;
    uint32_t f = drive(d, true, false, 0, 1000, 700);
    TEST_ASSERT_TRUE(f & CROSSMODAL_RADAR_UNCORROBORATED);
    TEST_ASSERT_FALSE(f & CROSSMODAL_CSI_UNCORROBORATED);
}

void test_csi_without_radar_flags_after_window() {
    CrossModalLiveness d;
    uint32_t f = drive(d, false, true, 0, 1000, 700);
    TEST_ASSERT_TRUE(f & CROSSMODAL_CSI_UNCORROBORATED);
}

void test_below_min_active_never_flags() {
    CrossModalLiveness d;
    drive(d, true, false, 0, 1000, 30);
    uint32_t f = drive(d, false, false, 30000, 1000, 640);
    TEST_ASSERT_EQUAL_UINT32(CROSSMODAL_NONE, f);
}

void test_lagged_csi_counts_as_corroboration() {
    CrossModalLiveness d;
    d.corrLagMs = 8000;
    uint32_t f = CROSSMODAL_NONE;
    for (uint32_t t = 0; t < 700000; t += 10000) {
        f = d.update({true, false, true, t});
        f = d.update({false, true, true, t + 5000});
    }
    TEST_ASSERT_EQUAL_UINT32(CROSSMODAL_NONE, f);
}

void test_not_both_live_resets() {
    CrossModalLiveness d;
    drive(d, true, false, 0, 1000, 400);
    uint32_t f = d.update({true, false, false, 400000});
    TEST_ASSERT_EQUAL_UINT32(CROSSMODAL_NONE, f);
    f = drive(d, true, false, 401000, 1000, 100);
    TEST_ASSERT_EQUAL_UINT32(CROSSMODAL_NONE, f);
}

void test_render_reasons() {
    char buf[128];
    int n = renderCrossModal(
        CROSSMODAL_RADAR_UNCORROBORATED | CROSSMODAL_CSI_UNCORROBORATED,
        buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "radar"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "CSI"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_agreement_never_flags);
    RUN_TEST(test_radar_without_csi_flags_after_window);
    RUN_TEST(test_csi_without_radar_flags_after_window);
    RUN_TEST(test_below_min_active_never_flags);
    RUN_TEST(test_lagged_csi_counts_as_corroboration);
    RUN_TEST(test_not_both_live_resets);
    RUN_TEST(test_render_reasons);
    return UNITY_END();
}
