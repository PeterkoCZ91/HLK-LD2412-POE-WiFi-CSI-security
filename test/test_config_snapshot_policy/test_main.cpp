#include <unity.h>

#include "services/ConfigSnapshotPolicy.h"

#include <cstring>

void setUp() {}
void tearDown() {}

// Klíče s credential tokenem + oba user-poloviny páru jsou secret —
// parita s /api/config/export a getSnapshotJSON maskou.
static void test_secret_key_detection() {
    TEST_ASSERT_TRUE(configSnapshotKeyIsSecret("mqtt_pass"));
    TEST_ASSERT_TRUE(configSnapshotKeyIsSecret("auth_pass"));
    TEST_ASSERT_TRUE(configSnapshotKeyIsSecret("mqtt_user"));
    TEST_ASSERT_TRUE(configSnapshotKeyIsSecret("auth_user"));
    // budoucí klíče pokryté tokenovou detekcí (kdyby se dostaly do NVS_KEYS)
    TEST_ASSERT_TRUE(configSnapshotKeyIsSecret("tg_token"));
    TEST_ASSERT_TRUE(configSnapshotKeyIsSecret("csi_pass"));
    TEST_ASSERT_TRUE(configSnapshotKeyIsSecret("dc_webhook"));

    TEST_ASSERT_FALSE(configSnapshotKeyIsSecret("mqtt_server"));
    TEST_ASSERT_FALSE(configSnapshotKeyIsSecret("hostname"));
    TEST_ASSERT_FALSE(configSnapshotKeyIsSecret("zones_json"));
    TEST_ASSERT_FALSE(configSnapshotKeyIsSecret("mqtt_id"));
    TEST_ASSERT_FALSE(configSnapshotKeyIsSecret(nullptr));
}

// Save-time maska: neprázdná secret hodnota -> "***", prázdná zůstává
// prázdná (slot má ukazovat "nenakonfigurováno"), ne-secret beze změny.
static void test_masked_string_for_save() {
    TEST_ASSERT_EQUAL_STRING("***", configSnapshotMaskedString("mqtt_pass", "s3cret-example"));
    TEST_ASSERT_EQUAL_STRING("***", configSnapshotMaskedString("auth_user", "admin"));
    TEST_ASSERT_EQUAL_STRING("", configSnapshotMaskedString("mqtt_pass", ""));
    TEST_ASSERT_EQUAL_STRING("broker.lan", configSnapshotMaskedString("mqtt_server", "broker.lan"));
    TEST_ASSERT_EQUAL_STRING("", configSnapshotMaskedString("mqtt_server", ""));
}

// Restore přeskočí sentinel "***" (neklobuje provisionovaná tajemství),
// raw hodnoty ze starých snapshotů se obnoví, nullptr se nikdy nezapisuje.
static void test_restore_skip_decision() {
    TEST_ASSERT_FALSE(configSnapshotShouldRestoreString("***"));
    TEST_ASSERT_FALSE(configSnapshotShouldRestoreString(nullptr));
    TEST_ASSERT_TRUE(configSnapshotShouldRestoreString("legacy-raw-password"));
    TEST_ASSERT_TRUE(configSnapshotShouldRestoreString(""));
    TEST_ASSERT_TRUE(configSnapshotShouldRestoreString("**"));
    TEST_ASSERT_TRUE(configSnapshotShouldRestoreString("****"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_secret_key_detection);
    RUN_TEST(test_masked_string_for_save);
    RUN_TEST(test_restore_skip_decision);
    return UNITY_END();
}
