#include <unity.h>

#include "services/ConfigImportValidation.h"

void setUp() {}
void tearDown() {}

static void test_text_bounds() {
    TEST_ASSERT_TRUE(configImportTextFits("", 0, 4));
    TEST_ASSERT_TRUE(configImportTextFits("abcd", 4, 4));
    TEST_ASSERT_FALSE(configImportTextFits("abc", 4, 8));
    TEST_ASSERT_FALSE(configImportTextFits("abcde", 0, 4));
    TEST_ASSERT_FALSE(configImportTextFits(nullptr, 0, 4));
}

static void test_redacted_marker() {
    TEST_ASSERT_TRUE(configImportValueIsRedacted("***"));
    TEST_ASSERT_FALSE(configImportValueIsRedacted(""));
    TEST_ASSERT_FALSE(configImportValueIsRedacted("**"));
    TEST_ASSERT_FALSE(configImportValueIsRedacted(nullptr));
}

static void test_port_validation() {
    TEST_ASSERT_TRUE(configImportPortValid("1"));
    TEST_ASSERT_TRUE(configImportPortValid("65535"));
    TEST_ASSERT_FALSE(configImportPortValid("0"));
    TEST_ASSERT_FALSE(configImportPortValid("65536"));
    TEST_ASSERT_FALSE(configImportPortValid("1883x"));
}

static void test_ipv4_or_empty_validation() {
    TEST_ASSERT_TRUE(configImportIpv4OrEmptyValid(""));
    TEST_ASSERT_TRUE(configImportIpv4OrEmptyValid("10.20.30.40"));
    TEST_ASSERT_TRUE(configImportIpv4OrEmptyValid("255.255.255.255"));
    TEST_ASSERT_FALSE(configImportIpv4OrEmptyValid("10.20.30"));
    TEST_ASSERT_FALSE(configImportIpv4OrEmptyValid("10.20.30.256"));
    TEST_ASSERT_FALSE(configImportIpv4OrEmptyValid("10.20..40"));
}

static void test_schedule_validation() {
    TEST_ASSERT_TRUE(configImportScheduleValid(""));
    TEST_ASSERT_TRUE(configImportScheduleValid("00:00"));
    TEST_ASSERT_TRUE(configImportScheduleValid("23:59"));
    TEST_ASSERT_FALSE(configImportScheduleValid("24:00"));
    TEST_ASSERT_FALSE(configImportScheduleValid("12:60"));
    TEST_ASSERT_FALSE(configImportScheduleValid("7:30"));
}

static void test_radar_resolution_validation() {
    TEST_ASSERT_TRUE(configImportRadarResolutionValid(0.20f));
    TEST_ASSERT_TRUE(configImportRadarResolutionValid(0.50f));
    TEST_ASSERT_TRUE(configImportRadarResolutionValid(0.75f));
    TEST_ASSERT_FALSE(configImportRadarResolutionValid(0.70f));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_text_bounds);
    RUN_TEST(test_redacted_marker);
    RUN_TEST(test_port_validation);
    RUN_TEST(test_ipv4_or_empty_validation);
    RUN_TEST(test_schedule_validation);
    RUN_TEST(test_radar_resolution_validation);
    return UNITY_END();
}
