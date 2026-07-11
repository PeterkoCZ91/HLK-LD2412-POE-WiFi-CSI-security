// Native unit tests for CSI candidate/active/previous model management.
// Pure — no Arduino. Run: pio test -e native -f test_csi_model
#include <unity.h>
#include "services/CsiSiteModel.h"
#include "services/FakeCsiModelStore.h"
#include "services/CsiModelManager.h"

void setUp() {}
void tearDown() {}

// ---- helpers ---------------------------------------------------------------
static CsiSiteModel goodModel() {
    CsiSiteModel m;
    // threshold must be >= CSI_MODEL_MIN_THRESHOLD (0.005) — the learning floor
    // clamps below that, so a valid model never carries a lower threshold.
    m.valid = true;
    m.threshold = 0.006f; m.meanVariance = 0.002f; m.stdVariance = 0.0005f;
    m.maxVariance = 0.003f; m.sampleCount = 6812; m.generation = 5;
    csiModelSeal(m);
    return m;
}
static CsiSiteModel candGen(uint32_t g) { CsiSiteModel c = goodModel(); c.generation = g; return c; }

// ---- Task 1: CRC / checksum ------------------------------------------------
void test_seal_makes_checksum_valid() {
    CsiSiteModel m; m.threshold = 0.0037f; m.generation = 5; m.sampleCount = 6812;
    csiModelSeal(m);
    TEST_ASSERT_TRUE(csiModelChecksumValid(m));
}
void test_mutation_after_seal_breaks_checksum() {
    CsiSiteModel m; m.threshold = 0.0037f; csiModelSeal(m);
    m.threshold = 0.9f;
    TEST_ASSERT_FALSE(csiModelChecksumValid(m));
}
void test_crc32_known_vector() {
    const char* s = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, csiModelCrc32(s, 9));
}

// ---- Task 2: validation ----------------------------------------------------
void test_validate_ok()                 { TEST_ASSERT_EQUAL(CsiModelValidity::OK, csiModelValidate(goodModel())); }
void test_validate_rejects_low_threshold() {
    CsiSiteModel m = goodModel(); m.threshold = 0.0001f; csiModelSeal(m);
    TEST_ASSERT_EQUAL(CsiModelValidity::THRESHOLD_RANGE, csiModelValidate(m));
}
void test_validate_rejects_nan() {
    CsiSiteModel m = goodModel(); m.meanVariance = NAN; csiModelSeal(m);
    TEST_ASSERT_EQUAL(CsiModelValidity::NAN_INF, csiModelValidate(m));
}
void test_validate_rejects_max_lt_mean() {
    CsiSiteModel m = goodModel(); m.maxVariance = 0.001f; m.meanVariance = 0.002f; csiModelSeal(m);
    TEST_ASSERT_EQUAL(CsiModelValidity::MAX_LT_MEAN, csiModelValidate(m));
}
void test_validate_rejects_few_samples() {
    CsiSiteModel m = goodModel(); m.sampleCount = 10; csiModelSeal(m);
    TEST_ASSERT_EQUAL(CsiModelValidity::TOO_FEW_SAMPLES, csiModelValidate(m));
}
void test_validate_rejects_bad_checksum() {
    CsiSiteModel m = goodModel(); m.threshold = 0.5f;   // not resealed
    TEST_ASSERT_EQUAL(CsiModelValidity::BAD_CHECKSUM, csiModelValidate(m));
}

// ---- Task 3: fake store ----------------------------------------------------
void test_fake_store_roundtrip() {
    FakeCsiModelStore s; CsiSiteModel m = goodModel(); CsiSiteModel out;
    TEST_ASSERT_TRUE(s.writeSlot(CsiModelSlot::ACTIVE, m));
    TEST_ASSERT_TRUE(s.readSlot(CsiModelSlot::ACTIVE, out));
    TEST_ASSERT_EQUAL_UINT32(m.generation, out.generation);
}
void test_fake_store_read_missing_fails() {
    FakeCsiModelStore s; CsiSiteModel out;
    TEST_ASSERT_FALSE(s.readSlot(CsiModelSlot::PREVIOUS, out));
}
void test_fake_store_injected_write_failure() {
    FakeCsiModelStore s; s.failSlotMask = (1 << (int)CsiModelSlot::ACTIVE);
    TEST_ASSERT_FALSE(s.writeSlot(CsiModelSlot::ACTIVE, goodModel()));
}

// ---- Task 4: finalize ------------------------------------------------------
void test_finalize_creates_candidate_not_active() {
    FakeCsiModelStore s; CsiModelManager mgr(s);
    CsiSiteModel a = goodModel(); a.generation = 4; a.threshold = 0.005f;
    s.writeSlot(CsiModelSlot::ACTIVE, a);
    mgr.loadFromStore();
    TEST_ASSERT_EQUAL(CsiModelOp::OK, mgr.finalizeCandidate(candGen(5)));
    TEST_ASSERT_TRUE(mgr.hasCandidate());
    TEST_ASSERT_EQUAL_UINT32(5, mgr.candidate().generation);
    TEST_ASSERT_EQUAL_UINT32(4, mgr.active().generation);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.005f, mgr.active().threshold);
}
void test_finalize_rejects_invalid() {
    FakeCsiModelStore s; CsiModelManager mgr(s);
    CsiSiteModel bad = goodModel(); bad.sampleCount = 5;
    TEST_ASSERT_EQUAL(CsiModelOp::VALIDATION_FAILED, mgr.finalizeCandidate(bad));
    TEST_ASSERT_FALSE(mgr.hasCandidate());
}
void test_next_generation_monotonic() {
    FakeCsiModelStore s; CsiModelManager mgr(s);
    TEST_ASSERT_EQUAL_UINT32(1, mgr.nextGeneration());
    TEST_ASSERT_EQUAL_UINT32(2, mgr.nextGeneration());
}

// ---- Task 5: apply ---------------------------------------------------------
void test_apply_moves_active_to_previous_and_candidate_to_active() {
    FakeCsiModelStore s; CsiModelManager mgr(s);
    CsiSiteModel a = goodModel(); a.generation = 4; a.threshold = 0.005f;
    s.writeSlot(CsiModelSlot::ACTIVE, a); mgr.loadFromStore();
    mgr.finalizeCandidate(candGen(5));
    TEST_ASSERT_EQUAL(CsiModelOp::OK, mgr.applyCandidate());
    TEST_ASSERT_EQUAL_UINT32(5, mgr.active().generation);
    TEST_ASSERT_EQUAL_UINT32(4, mgr.previous().generation);
    TEST_ASSERT_TRUE(mgr.hasPrevious());
}
void test_apply_without_candidate_fails() {
    FakeCsiModelStore s; CsiModelManager mgr(s);
    TEST_ASSERT_EQUAL(CsiModelOp::NO_CANDIDATE, mgr.applyCandidate());
}
void test_apply_previous_write_failure_keeps_active() {
    FakeCsiModelStore s; CsiModelManager mgr(s);
    CsiSiteModel a = goodModel(); a.generation = 4;
    s.writeSlot(CsiModelSlot::ACTIVE, a); mgr.loadFromStore();
    mgr.finalizeCandidate(candGen(5));
    s.failSlotMask = (1 << (int)CsiModelSlot::PREVIOUS);
    TEST_ASSERT_EQUAL(CsiModelOp::STORE_FAILED, mgr.applyCandidate());
    TEST_ASSERT_EQUAL_UINT32(4, mgr.active().generation);
}
void test_apply_active_write_failure_keeps_active() {
    FakeCsiModelStore s; CsiModelManager mgr(s);
    CsiSiteModel a = goodModel(); a.generation = 4;
    s.writeSlot(CsiModelSlot::ACTIVE, a); mgr.loadFromStore();
    mgr.finalizeCandidate(candGen(5));
    s.failSlotMask = (1 << (int)CsiModelSlot::ACTIVE);
    TEST_ASSERT_EQUAL(CsiModelOp::STORE_FAILED, mgr.applyCandidate());
    TEST_ASSERT_EQUAL_UINT32(4, mgr.active().generation);
}

// ---- Task 6: rollback ------------------------------------------------------
void test_rollback_swaps_active_and_previous() {
    FakeCsiModelStore s; CsiModelManager mgr(s);
    CsiSiteModel a = goodModel(); a.generation = 4; s.writeSlot(CsiModelSlot::ACTIVE, a);
    mgr.loadFromStore();
    mgr.finalizeCandidate(candGen(5));
    mgr.applyCandidate();
    TEST_ASSERT_EQUAL(CsiModelOp::OK, mgr.rollback());
    TEST_ASSERT_EQUAL_UINT32(4, mgr.active().generation);
    TEST_ASSERT_EQUAL_UINT32(5, mgr.previous().generation);
}
void test_rollback_without_previous_fails() {
    FakeCsiModelStore s; CsiModelManager mgr(s);
    TEST_ASSERT_EQUAL(CsiModelOp::NO_PREVIOUS, mgr.rollback());
}

// ---- Task 7: clear candidate ----------------------------------------------
void test_clear_candidate_keeps_active_previous() {
    FakeCsiModelStore s; CsiModelManager mgr(s);
    CsiSiteModel a = goodModel(); a.generation = 4; s.writeSlot(CsiModelSlot::ACTIVE, a);
    mgr.loadFromStore();
    mgr.finalizeCandidate(candGen(5));
    mgr.applyCandidate();
    TEST_ASSERT_EQUAL(CsiModelOp::OK, mgr.clearCandidate());
    TEST_ASSERT_FALSE(mgr.hasCandidate());
    TEST_ASSERT_EQUAL_UINT32(5, mgr.active().generation);
    TEST_ASSERT_EQUAL_UINT32(4, mgr.previous().generation);
}

// ---- Task 8: legacy migration ---------------------------------------------
void test_migrate_legacy_creates_active() {
    FakeCsiModelStore s; CsiModelManager mgr(s); mgr.loadFromStore();
    CsiLegacyModel lg{ true, 0.005f, 0.002f, 0.0005f, 0.003f, 0.1f, 0.2f, 0.3f, 3462 };
    TEST_ASSERT_EQUAL(CsiModelOp::OK, mgr.migrateLegacy(lg));
    TEST_ASSERT_TRUE(mgr.active().valid);
    TEST_ASSERT_EQUAL_UINT32(3462, mgr.active().sampleCount);
    TEST_ASSERT_TRUE(mgr.active().generation >= 1);
}
void test_migrate_noop_when_active_exists() {
    FakeCsiModelStore s; CsiModelManager mgr(s);
    CsiSiteModel a = goodModel(); a.generation = 9; s.writeSlot(CsiModelSlot::ACTIVE, a);
    mgr.loadFromStore();
    CsiLegacyModel lg{ true, 0.01f, 0, 0, 0.02f, 0, 0, 0, 100 };
    TEST_ASSERT_EQUAL(CsiModelOp::OK, mgr.migrateLegacy(lg));
    TEST_ASSERT_EQUAL_UINT32(9, mgr.active().generation);
}

// ---- Task 13 (unit part): EMA touches only active --------------------------
void test_ema_updates_only_active() {
    FakeCsiModelStore s; CsiModelManager mgr(s);
    CsiSiteModel a = goodModel(); a.generation = 4; a.threshold = 0.005f; csiModelSeal(a);
    s.writeSlot(CsiModelSlot::ACTIVE, a);
    mgr.loadFromStore();
    mgr.finalizeCandidate(candGen(5));  // candidate threshold 0.006
    mgr.applyCandidate();  // active=5 (0.006), prev=4 (0.005)
    uint32_t candGenBefore = mgr.candidate().generation;
    uint32_t prevGenBefore = mgr.previous().generation;
    TEST_ASSERT_EQUAL(CsiModelOp::OK, mgr.updateActiveEma(0.007f, 0.0025f, 0.0006f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.007f, mgr.active().threshold);
    TEST_ASSERT_EQUAL_UINT32(candGenBefore, mgr.candidate().generation);
    TEST_ASSERT_EQUAL_UINT32(prevGenBefore, mgr.previous().generation);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.005f, mgr.previous().threshold); // prev (gen4) untouched
}

// ---- Task 9: quality report + histogram -----------------------------------
void test_histogram_quantile_monotone() {
    CsiVarianceHistogram h;
    for (int i = 0; i < 1000; i++) h.add(0.002f);
    float p50 = h.quantile(0.5f), p99 = h.quantile(0.99f);
    TEST_ASSERT_TRUE(p99 >= p50);
    TEST_ASSERT_TRUE(p50 > 0.0f);
}
void test_quality_checksum_roundtrip() {
    CsiModelQuality q; q.generation = 5; q.accepted = 6812; q.p95 = 0.003f;
    csiQualitySeal(q);
    TEST_ASSERT_TRUE(csiQualityChecksumValid(q));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_seal_makes_checksum_valid);
    RUN_TEST(test_mutation_after_seal_breaks_checksum);
    RUN_TEST(test_crc32_known_vector);
    RUN_TEST(test_validate_ok);
    RUN_TEST(test_validate_rejects_low_threshold);
    RUN_TEST(test_validate_rejects_nan);
    RUN_TEST(test_validate_rejects_max_lt_mean);
    RUN_TEST(test_validate_rejects_few_samples);
    RUN_TEST(test_validate_rejects_bad_checksum);
    RUN_TEST(test_fake_store_roundtrip);
    RUN_TEST(test_fake_store_read_missing_fails);
    RUN_TEST(test_fake_store_injected_write_failure);
    RUN_TEST(test_finalize_creates_candidate_not_active);
    RUN_TEST(test_finalize_rejects_invalid);
    RUN_TEST(test_next_generation_monotonic);
    RUN_TEST(test_apply_moves_active_to_previous_and_candidate_to_active);
    RUN_TEST(test_apply_without_candidate_fails);
    RUN_TEST(test_apply_previous_write_failure_keeps_active);
    RUN_TEST(test_apply_active_write_failure_keeps_active);
    RUN_TEST(test_rollback_swaps_active_and_previous);
    RUN_TEST(test_rollback_without_previous_fails);
    RUN_TEST(test_clear_candidate_keeps_active_previous);
    RUN_TEST(test_migrate_legacy_creates_active);
    RUN_TEST(test_migrate_noop_when_active_exists);
    RUN_TEST(test_ema_updates_only_active);
    RUN_TEST(test_histogram_quantile_monotone);
    RUN_TEST(test_quality_checksum_roundtrip);
    return UNITY_END();
}
