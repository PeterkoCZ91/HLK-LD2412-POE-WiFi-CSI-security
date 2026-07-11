// Native unit tests for CSI model export helpers (P1.5, navrh 17.5/17.11).
// Pure — no Arduino. Run: pio test -e native -f test_csi_export
#include <unity.h>
#include "services/CsiModelExport.h"

void setUp() {}
void tearDown() {}

void test_unset_bssid_hashes_to_zero() {
    uint8_t z[6] = {0,0,0,0,0,0};
    TEST_ASSERT_EQUAL_UINT32(0, csiBssidHash(z));
}

void test_bssid_hash_is_deterministic() {
    uint8_t a[6] = {0xDE,0xAD,0xBE,0xEF,0x00,0x11};
    TEST_ASSERT_EQUAL_UINT32(csiBssidHash(a), csiBssidHash(a));
}

void test_different_bssid_differs() {
    uint8_t a[6] = {0xDE,0xAD,0xBE,0xEF,0x00,0x11};
    uint8_t b[6] = {0xDE,0xAD,0xBE,0xEF,0x00,0x12};
    TEST_ASSERT_NOT_EQUAL(csiBssidHash(a), csiBssidHash(b));
}

void test_nonzero_bssid_hashes_nonzero() {
    uint8_t a[6] = {0x00,0x00,0x00,0x00,0x00,0x01};
    TEST_ASSERT_NOT_EQUAL(0, csiBssidHash(a));
}

// The whole safety story of import: a model reconstructed from an export must
// still pass the same validation that guards finalize/apply. This mirrors what
// the import endpoint relies on.
void test_reconstructed_model_validates() {
    CsiSiteModel m;
    m.threshold = 0.006f; m.meanVariance = 0.002f; m.stdVariance = 0.0005f;
    m.maxVariance = 0.003f; m.sampleCount = 6812; m.generation = 9;
    m.schemaVersion = CSI_MODEL_SCHEMA;
    csiModelSeal(m);
    TEST_ASSERT_EQUAL(CsiModelValidity::OK, csiModelValidate(m));
}

void test_reconstructed_below_floor_rejected() {
    CsiSiteModel m;
    m.threshold = 0.001f;  // under absolute floor → import must fail validation
    m.meanVariance = 0.0005f; m.stdVariance = 0.0001f; m.maxVariance = 0.0008f;
    m.sampleCount = 6812;
    csiModelSeal(m);
    TEST_ASSERT_EQUAL(CsiModelValidity::THRESHOLD_RANGE, csiModelValidate(m));
}

void test_algo_compat_constant_present() {
    TEST_ASSERT_EQUAL_UINT32(1, CSI_MODEL_ALGO_COMPAT);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_unset_bssid_hashes_to_zero);
    RUN_TEST(test_bssid_hash_is_deterministic);
    RUN_TEST(test_different_bssid_differs);
    RUN_TEST(test_nonzero_bssid_hashes_nonzero);
    RUN_TEST(test_reconstructed_model_validates);
    RUN_TEST(test_reconstructed_below_floor_rejected);
    RUN_TEST(test_algo_compat_constant_present);
    return UNITY_END();
}
