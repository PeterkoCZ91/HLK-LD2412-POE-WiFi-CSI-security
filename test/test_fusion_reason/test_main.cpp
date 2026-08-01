// Native unit tests for the fusion-reason string helper (#8 fusion panel).
// Run: pio test -e native -f test_fusion_reason
#include <unity.h>
#include <cstring>
#include "services/FusionReason.h"

void setUp() {}
void tearDown() {}

static const char* reason(uint8_t src, float c) {
    static char b[96];
    fusionReasonStr(src, c, b, sizeof(b));
    return b;
}

void test_triple_agreement() {
    TEST_ASSERT_NOT_NULL(strstr(reason(FUSION_RADAR | FUSION_CSI | FUSION_ML, 1.0f), "agree"));
}

void test_radar_ml_csi_disagrees() {
    const char* r = reason(FUSION_RADAR | FUSION_ML, 0.6f);
    TEST_ASSERT_NOT_NULL(strstr(r, "radar"));
    TEST_ASSERT_NOT_NULL(strstr(r, "CSI"));   // mentions CSI disagreeing
}

void test_csi_only() {
    TEST_ASSERT_NOT_NULL(strstr(reason(FUSION_CSI, 0.4f), "CSI only"));
}

void test_quiet_no_source() {
    TEST_ASSERT_NOT_NULL(strstr(reason(0, 0.0f), "quiet"));
}

void test_confidence_rendered() {
    TEST_ASSERT_NOT_NULL(strstr(reason(FUSION_RADAR | FUSION_CSI | FUSION_ML, 0.9f), "90%"));
}

void test_buffer_never_overflows() {
    char tiny[8];
    int n = fusionReasonStr(FUSION_RADAR | FUSION_CSI | FUSION_ML, 1.0f, tiny, sizeof(tiny));
    TEST_ASSERT_TRUE(n < (int)sizeof(tiny));
    TEST_ASSERT_EQUAL_CHAR('\0', tiny[sizeof(tiny) - 1]);  // NUL-terminated within buf
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_triple_agreement);
    RUN_TEST(test_radar_ml_csi_disagrees);
    RUN_TEST(test_csi_only);
    RUN_TEST(test_quiet_no_source);
    RUN_TEST(test_confidence_rendered);
    RUN_TEST(test_buffer_never_overflows);
    return UNITY_END();
}
