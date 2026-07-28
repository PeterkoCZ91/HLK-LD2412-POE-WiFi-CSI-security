#include <cstring>
#include <unity.h>

#include "services/SensitiveDataRedaction.h"

void setUp() {}
void tearDown() {}

void test_sensitive_key_policy_covers_credentials() {
    TEST_ASSERT_TRUE(isSensitiveKeyName("mqtt_pass"));
    TEST_ASSERT_TRUE(isSensitiveKeyName("auth_password"));
    TEST_ASSERT_TRUE(isSensitiveKeyName("telegram_token"));
    TEST_ASSERT_TRUE(isSensitiveKeyName("discord_webhook"));
    TEST_ASSERT_TRUE(isSensitiveKeyName("api.secret"));
    TEST_ASSERT_TRUE(isSensitiveKeyName("alarm_pin"));
    TEST_ASSERT_TRUE(isSensitiveKeyName("apikey"));
}

void test_sensitive_key_policy_keeps_diagnostics() {
    TEST_ASSERT_FALSE(isSensitiveKeyName("mqtt_server"));
    TEST_ASSERT_FALSE(isSensitiveKeyName("hostname"));
    TEST_ASSERT_FALSE(isSensitiveKeyName("publish_fail_streak"));
}

void test_redacts_key_value_and_json_forms() {
    char text[] = "mqtt_pass=hunter2 token: abc123 \"auth_password\":\"letmein\" host=lan";
    redactSensitiveText(text, sizeof(text));
    TEST_ASSERT_NULL(std::strstr(text, "hunter2"));
    TEST_ASSERT_NULL(std::strstr(text, "abc123"));
    TEST_ASSERT_NULL(std::strstr(text, "letmein"));
    TEST_ASSERT_NOT_NULL(std::strstr(text, "host=lan"));
}

void test_redacts_url_userinfo_and_sensitive_query() {
    char text[] = "pull https://user:pass@device.lan/fw.bin?token=abc&slot=1";
    redactSensitiveText(text, sizeof(text));
    TEST_ASSERT_NULL(std::strstr(text, "user:pass"));
    TEST_ASSERT_NULL(std::strstr(text, "token=abc"));
    TEST_ASSERT_NOT_NULL(std::strstr(text, "device.lan/fw.bin"));
    TEST_ASSERT_NOT_NULL(std::strstr(text, "slot=1"));
}

void test_null_and_truncated_buffers_are_safe() {
    redactSensitiveText(nullptr, 0);
    char text[5] = {'p', 'a', 's', 's', '='};
    redactSensitiveText(text, sizeof(text));
    TEST_ASSERT_EQUAL_MEMORY("pass=", text, sizeof(text));
}

void test_unterminated_quote_does_not_stop_later_redaction() {
    char text[] = "note='unfinished mqtt_pass=hunter2 host=lan";
    redactSensitiveText(text, sizeof(text));
    TEST_ASSERT_NULL(std::strstr(text, "hunter2"));
    TEST_ASSERT_NOT_NULL(std::strstr(text, "host=lan"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sensitive_key_policy_covers_credentials);
    RUN_TEST(test_sensitive_key_policy_keeps_diagnostics);
    RUN_TEST(test_redacts_key_value_and_json_forms);
    RUN_TEST(test_redacts_url_userinfo_and_sensitive_query);
    RUN_TEST(test_null_and_truncated_buffers_are_safe);
    RUN_TEST(test_unterminated_quote_does_not_stop_later_redaction);
    return UNITY_END();
}
