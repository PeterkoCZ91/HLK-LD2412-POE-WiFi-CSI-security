#include <unity.h>
#include "services/OtaTlsTrustPolicy.h"

void setUp() {}
void tearDown() {}

static void test_valid_pem_and_bounds() {
    const char* pem = "-----BEGIN CERTIFICATE-----\nabc\n-----END CERTIFICATE-----\n";
    TEST_ASSERT_TRUE(otaTlsPemValid(pem));
    TEST_ASSERT_FALSE(otaTlsPemValid(""));
    TEST_ASSERT_FALSE(otaTlsPemValid("-----BEGIN CERTIFICATE-----\nabc"));
    TEST_ASSERT_FALSE(otaTlsPemValid("x-----BEGIN CERTIFICATE-----\n-----END CERTIFICATE-----"));
}

static void test_only_http_and_https_are_supported() {
    TEST_ASSERT_TRUE(otaTlsHttpUrl("http://192.168.1.2/fw.bin"));
    TEST_ASSERT_TRUE(otaTlsHttpsUrl("https://update.local/fw.bin"));
    TEST_ASSERT_TRUE(otaTlsSupportedUrl("https://update.local/fw.bin"));
    TEST_ASSERT_FALSE(otaTlsSupportedUrl("ftp://update.local/fw.bin"));
    TEST_ASSERT_FALSE(otaTlsSupportedUrl(nullptr));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_pem_and_bounds);
    RUN_TEST(test_only_http_and_https_are_supported);
    return UNITY_END();
}
