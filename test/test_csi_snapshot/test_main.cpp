#include <unity.h>
#include <atomic>
#include <thread>
#include "services/CsiDetectionSnapshot.h"

void setUp() {}
void tearDown() {}

void test_snapshot_defaults_are_fail_safe() {
    CsiDetectionSnapshotStore store;
    CsiDetectionSnapshot snapshot;
    TEST_ASSERT_TRUE(store.read(snapshot));
    TEST_ASSERT_FALSE(snapshot.motion);
    TEST_ASSERT_FALSE(snapshot.mlMotion);
    TEST_ASSERT_TRUE(snapshot.dataOk);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.seq);
}

void test_reader_never_accepts_a_torn_generation() {
    CsiDetectionSnapshotStore store;
    std::atomic<bool> running{true};
    std::atomic<uint32_t> checked{0};

    std::thread writer([&]() {
        for (uint32_t generation = 1; generation <= 50000; generation++) {
            CsiDetectionSnapshot value;
            value.motion = (generation & 1U) != 0;
            value.mlMotion = value.motion;
            value.mlEnabled = true;
            value.compositeScore = static_cast<float>(generation);
            value.breathingScore = static_cast<float>(generation * 2U);
            value.mlProbability = static_cast<float>(generation * 3U);
            value.mlThreshold = static_cast<float>(generation * 4U);
            value.dataOk = true;
            store.publish(value);
        }
        running.store(false);
    });

    do {
        CsiDetectionSnapshot snapshot;
        if (!store.read(snapshot)) continue;
        if (snapshot.seq == 0) continue;
        TEST_ASSERT_EQUAL(snapshot.motion, snapshot.mlMotion);
        TEST_ASSERT_EQUAL_FLOAT(snapshot.compositeScore * 2.0f, snapshot.breathingScore);
        TEST_ASSERT_EQUAL_FLOAT(snapshot.compositeScore * 3.0f, snapshot.mlProbability);
        TEST_ASSERT_EQUAL_FLOAT(snapshot.compositeScore * 4.0f, snapshot.mlThreshold);
        TEST_ASSERT_EQUAL_UINT32(0, snapshot.seq & 1U);
        checked.fetch_add(1);
    } while (running.load());

    writer.join();
    TEST_ASSERT_GREATER_THAN_UINT32(0, checked.load());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_snapshot_defaults_are_fail_safe);
    RUN_TEST(test_reader_never_accepts_a_torn_generation);
    return UNITY_END();
}
