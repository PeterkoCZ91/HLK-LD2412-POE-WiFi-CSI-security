#include <unity.h>
#include <cmath>
#include "services/CsiAdaptiveThreshold.h"

void setUp() {}
void tearDown() {}

// nearest-rank index for the two percentiles the feature ships (P95 default, P99).
void test_percentile_index_p95_p99() {
    TEST_ASSERT_EQUAL_UINT16(284, csiPercentileIndex(300, 0.95f)); // (299)*0.95=284.05
    TEST_ASSERT_EQUAL_UINT16(296, csiPercentileIndex(300, 0.99f)); // (299)*0.99=296.01
    TEST_ASSERT_EQUAL_UINT16(94,  csiPercentileIndex(100, 0.95f)); // (99)*0.95=94.05
    TEST_ASSERT_EQUAL_UINT16(98,  csiPercentileIndex(100, 0.99f)); // (99)*0.99=98.01
    TEST_ASSERT_EQUAL_UINT16(99,  csiPercentileIndex(100, 1.0f));  // clamps to last
    TEST_ASSERT_EQUAL_UINT16(0,   csiPercentileIndex(0,   0.95f)); // empty
}

// P99 threshold must sit on or above P95 on the same data (stricter tail).
void test_p99_ge_p95_scaled() {
    float p95buf[100], p99buf[100];
    for (int i = 0; i < 100; i++) { p95buf[i] = (float)(i + 1); p99buf[i] = (float)(i + 1); }
    float t95 = csiAdaptiveThreshold(p95buf, 100, 0.95f, 1.1f); // idx94 -> 95 *1.1
    float t99 = csiAdaptiveThreshold(p99buf, 100, 0.99f, 1.1f); // idx98 -> 99 *1.1
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 95.0f * 1.1f, t95);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 99.0f * 1.1f, t99);
    TEST_ASSERT_TRUE(t99 > t95);
}

// nth_element must yield the correct quantile regardless of input ordering.
void test_order_independent() {
    float asc[50], desc[50];
    for (int i = 0; i < 50; i++) { asc[i] = (float)(i + 1); desc[i] = (float)(50 - i); }
    float ta = csiAdaptiveThreshold(asc,  50, 0.95f, 1.0f);
    float td = csiAdaptiveThreshold(desc, 50, 0.95f, 1.0f);
    // idx = (49)*0.95 = 46.55 -> 46 -> value 47
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 47.0f, ta);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 47.0f, td);
}

void test_clamp_band() {
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.95f,  csiClampAdaptivePercentile(0.95f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.99f,  csiClampAdaptivePercentile(0.99f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.999f, csiClampAdaptivePercentile(2.0f));   // over -> hi edge
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.50f,  csiClampAdaptivePercentile(-1.0f));  // under -> lo edge
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.95f,  csiClampAdaptivePercentile(NAN));    // NaN -> default
}

void test_empty_and_null() {
    float buf[1] = {1.0f};
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, csiAdaptiveThreshold(buf, 0, 0.95f, 1.1f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, csiAdaptiveThreshold(nullptr, 10, 0.95f, 1.1f));
}

// API input must parse strictly: Arduino toFloat() returns 0 for garbage, which
// the clamp would turn into a persisted P50 on an armed node. Fractions and
// whole percents are the two documented forms; anything else is rejected.
void test_parse_percentile_accepts_fraction_and_percent() {
    float out = 0.0f;
    TEST_ASSERT_TRUE(csiParseAdaptivePercentile("0.95", &out));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.95f, out);
    TEST_ASSERT_TRUE(csiParseAdaptivePercentile("0.99", &out));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.99f, out);
    TEST_ASSERT_TRUE(csiParseAdaptivePercentile("95", &out));     // whole percent
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.95f, out);
    TEST_ASSERT_TRUE(csiParseAdaptivePercentile("99.9", &out));   // percent, band top
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.999f, out);
    TEST_ASSERT_TRUE(csiParseAdaptivePercentile("0.5", &out));    // band bottom
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.50f, out);
    TEST_ASSERT_TRUE(csiParseAdaptivePercentile("0.999", &out));  // band top
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.999f, out);
}

void test_parse_percentile_rejects_garbage_and_out_of_band() {
    float out = 123.0f;
    TEST_ASSERT_FALSE(csiParseAdaptivePercentile("", &out));       // toFloat()=0 trap
    TEST_ASSERT_FALSE(csiParseAdaptivePercentile("abc", &out));
    TEST_ASSERT_FALSE(csiParseAdaptivePercentile("0,99", &out));   // decimal comma
    TEST_ASSERT_FALSE(csiParseAdaptivePercentile("0.95x", &out));  // trailing junk
    TEST_ASSERT_FALSE(csiParseAdaptivePercentile("0.4", &out));    // below band
    TEST_ASSERT_FALSE(csiParseAdaptivePercentile("0.9999", &out)); // above band
    TEST_ASSERT_FALSE(csiParseAdaptivePercentile("49", &out));     // percent below band
    TEST_ASSERT_FALSE(csiParseAdaptivePercentile("100", &out));    // 1.0 not allowed
    TEST_ASSERT_FALSE(csiParseAdaptivePercentile("nan", &out));
    TEST_ASSERT_FALSE(csiParseAdaptivePercentile("inf", &out));
    TEST_ASSERT_FALSE(csiParseAdaptivePercentile(nullptr, &out));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 123.0f, out);                  // out untouched
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_percentile_index_p95_p99);
    RUN_TEST(test_p99_ge_p95_scaled);
    RUN_TEST(test_order_independent);
    RUN_TEST(test_clamp_band);
    RUN_TEST(test_empty_and_null);
    RUN_TEST(test_parse_percentile_accepts_fraction_and_percent);
    RUN_TEST(test_parse_percentile_rejects_garbage_and_out_of_band);
    return UNITY_END();
}
