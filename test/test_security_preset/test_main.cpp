// Native tests for the security sensitivity preset mapping (#12).
// Run: pio test -e native -f test_security_preset
#include <unity.h>
#include <cstring>
#include "services/SecurityPreset.h"

void setUp() {}
void tearDown() {}

void test_parse_roundtrip() {
    SecPreset preset;
    TEST_ASSERT_TRUE(parseSecPreset("Home", preset));
    TEST_ASSERT_TRUE(preset == SecPreset::HOME);
    TEST_ASSERT_TRUE(parseSecPreset("Empty", preset));
    TEST_ASSERT_TRUE(preset == SecPreset::EMPTY);
    TEST_ASSERT_TRUE(parseSecPreset("Paranoid", preset));
    TEST_ASSERT_TRUE(preset == SecPreset::PARANOID);
    TEST_ASSERT_FALSE(parseSecPreset("bogus", preset));
    TEST_ASSERT_EQUAL_STRING("Home", secPresetName(SecPreset::HOME));
}

void test_paranoid_is_most_sensitive() {
    SecPresetParams empty = securityPresetParams(SecPreset::EMPTY);
    SecPresetParams home = securityPresetParams(SecPreset::HOME);
    SecPresetParams paranoid = securityPresetParams(SecPreset::PARANOID);
    TEST_ASSERT_TRUE(paranoid.alarmEnergyThreshold <= home.alarmEnergyThreshold);
    TEST_ASSERT_TRUE(home.alarmEnergyThreshold <= empty.alarmEnergyThreshold);
    TEST_ASSERT_TRUE(paranoid.entryDelayMs <= home.entryDelayMs);
    TEST_ASSERT_EQUAL_UINT8(0, paranoid.petImmunity);
    TEST_ASSERT_FALSE(paranoid.corroborationEnabled);
    TEST_ASSERT_TRUE(empty.corroborationEnabled);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_roundtrip);
    RUN_TEST(test_paranoid_is_most_sensitive);
    return UNITY_END();
}
