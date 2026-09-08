#include <unity.h>
#include <atomic>
#include <thread>
#include "services/MlLastInference.h"

void setUp() {}
void tearDown() {}

void test_empty_snapshot() {
    MlLastInferenceStore store;
    MlLastInference out;
    TEST_ASSERT_TRUE(store.read(out));
    TEST_ASSERT_FALSE(out.valid);
}

void test_concurrent_snapshot_is_one_generation() {
    MlLastInferenceStore store;
    std::atomic<bool> running{true};
    bool consistent = true;
    unsigned checked = 0;
    std::thread writer([&]() {
        for (uint32_t generation = 1; generation <= 50000; ++generation) {
            MlLastInference value;
            value.valid = true;
            value.uptimeMs = generation;
            for (unsigned k = 0; k < csi_ml::ML_NUM_FEATURES; ++k)
                value.feats[k] = static_cast<float>(generation + k);
            store.publish(value);
        }
        running.store(false);
    });
    do {
        MlLastInference out;
        if (!store.read(out) || !out.valid) continue;
        ++checked;
        for (unsigned k = 0; k < csi_ml::ML_NUM_FEATURES; ++k)
            consistent = consistent && out.feats[k] == static_cast<float>(out.uptimeMs + k);
    } while (running.load());
    writer.join();
    TEST_ASSERT_TRUE(consistent);
    TEST_ASSERT_GREATER_THAN(0, checked);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_snapshot);
    RUN_TEST(test_concurrent_snapshot_is_one_generation);
    return UNITY_END();
}
