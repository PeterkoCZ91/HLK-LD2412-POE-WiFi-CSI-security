#include <unity.h>
#include "services/RuntimeOperationCoordinator.h"

void test_only_one_operation_can_own_runtime() {
    RuntimeOperationCoordinator coordinator;
    TEST_ASSERT_TRUE(coordinator.tryBegin(RuntimeOperation::OTA, 100, 1000));
    TEST_ASSERT_FALSE(coordinator.tryBegin(RuntimeOperation::WIFI_SCAN, 101, 1000));
    TEST_ASSERT_EQUAL(static_cast<int>(RuntimeOperation::OTA),
                      static_cast<int>(coordinator.status().operation));
}

void test_progress_extends_watchdog_window() {
    RuntimeOperationCoordinator coordinator;
    TEST_ASSERT_TRUE(coordinator.tryBegin(RuntimeOperation::OTA, 100, 1000));
    TEST_ASSERT_TRUE(coordinator.markProgress(RuntimeOperation::OTA, 900));
    TEST_ASSERT_FALSE(coordinator.checkTimeout(1800));
    TEST_ASSERT_TRUE(coordinator.checkTimeout(1901));
    TEST_ASSERT_EQUAL(static_cast<int>(RuntimeOperation::IDLE),
                      static_cast<int>(coordinator.status().operation));
    TEST_ASSERT_EQUAL(static_cast<int>(RuntimeOperationFailure::TIMED_OUT),
                      static_cast<int>(coordinator.status().failure));
}

void test_stale_owner_cannot_finish_or_cancel_current_operation() {
    RuntimeOperationCoordinator coordinator;
    TEST_ASSERT_TRUE(coordinator.tryBegin(RuntimeOperation::OTA, 10, 50, 2));
    TEST_ASSERT_FALSE(coordinator.finish(RuntimeOperation::OTA, 1));
    TEST_ASSERT_FALSE(coordinator.cancel(RuntimeOperation::OTA, 1));
    TEST_ASSERT_FALSE(coordinator.markProgress(RuntimeOperation::OTA, 20, 1));
    TEST_ASSERT_EQUAL_UINT8(2, coordinator.status().ownerId);
    TEST_ASSERT_EQUAL_UINT32(10, coordinator.status().lastProgressMs);
    TEST_ASSERT_EQUAL(static_cast<int>(RuntimeOperation::OTA),
                      static_cast<int>(coordinator.status().operation));
}

void test_timeout_is_wrap_safe() {
    RuntimeOperationCoordinator coordinator;
    TEST_ASSERT_TRUE(coordinator.tryBegin(RuntimeOperation::WIFI_SCAN, 0xFFFFFFF0u, 40));
    TEST_ASSERT_FALSE(coordinator.checkTimeout(20));
    TEST_ASSERT_TRUE(coordinator.checkTimeout(25));
}

void test_timeout_reports_owner_before_releasing_runtime() {
    RuntimeOperationCoordinator coordinator;
    RuntimeOperationStatus timedOut;
    TEST_ASSERT_TRUE(coordinator.tryBegin(RuntimeOperation::OTA, 100, 50, 4));
    TEST_ASSERT_TRUE(coordinator.checkTimeout(151, &timedOut));
    TEST_ASSERT_EQUAL(static_cast<int>(RuntimeOperation::OTA),
                      static_cast<int>(timedOut.operation));
    TEST_ASSERT_EQUAL_UINT8(4, timedOut.ownerId);
    TEST_ASSERT_EQUAL_UINT32(100, timedOut.lastProgressMs);
    TEST_ASSERT_EQUAL_UINT32(50, timedOut.timeoutMs);
    TEST_ASSERT_EQUAL(static_cast<int>(RuntimeOperation::IDLE),
                      static_cast<int>(coordinator.status().operation));
}

void test_operation_specific_watchdog_does_not_release_another_operation() {
    RuntimeOperationCoordinator coordinator;
    RuntimeOperationStatus timedOut;
    TEST_ASSERT_TRUE(coordinator.tryBegin(RuntimeOperation::WIFI_SCAN, 10, 50));
    TEST_ASSERT_FALSE(coordinator.checkTimeout(RuntimeOperation::OTA, 100, &timedOut));
    TEST_ASSERT_EQUAL(static_cast<int>(RuntimeOperation::WIFI_SCAN),
                      static_cast<int>(coordinator.status().operation));
    TEST_ASSERT_TRUE(coordinator.checkTimeout(RuntimeOperation::WIFI_SCAN, 100,
                                              &timedOut));
    TEST_ASSERT_EQUAL(static_cast<int>(RuntimeOperation::WIFI_SCAN),
                      static_cast<int>(timedOut.operation));
}

void test_operation_and_failure_text_is_stable() {
    TEST_ASSERT_EQUAL_STRING("idle", runtimeOperationText(RuntimeOperation::IDLE));
    TEST_ASSERT_EQUAL_STRING("wifi_scan", runtimeOperationText(RuntimeOperation::WIFI_SCAN));
    TEST_ASSERT_EQUAL_STRING("calibration", runtimeOperationText(RuntimeOperation::CALIBRATION));
    TEST_ASSERT_EQUAL_STRING("site_learning", runtimeOperationText(RuntimeOperation::SITE_LEARNING));
    TEST_ASSERT_EQUAL_STRING("ota", runtimeOperationText(RuntimeOperation::OTA));
    TEST_ASSERT_EQUAL_STRING("restart", runtimeOperationText(RuntimeOperation::RESTART));
    TEST_ASSERT_EQUAL_STRING("none", runtimeOperationFailureText(RuntimeOperationFailure::NONE));
    TEST_ASSERT_EQUAL_STRING("busy", runtimeOperationFailureText(RuntimeOperationFailure::BUSY));
    TEST_ASSERT_EQUAL_STRING("timed_out", runtimeOperationFailureText(RuntimeOperationFailure::TIMED_OUT));
    TEST_ASSERT_EQUAL_STRING("cancelled", runtimeOperationFailureText(RuntimeOperationFailure::CANCELLED));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_only_one_operation_can_own_runtime);
    RUN_TEST(test_progress_extends_watchdog_window);
    RUN_TEST(test_stale_owner_cannot_finish_or_cancel_current_operation);
    RUN_TEST(test_timeout_is_wrap_safe);
    RUN_TEST(test_timeout_reports_owner_before_releasing_runtime);
    RUN_TEST(test_operation_specific_watchdog_does_not_release_another_operation);
    RUN_TEST(test_operation_and_failure_text_is_stable);
    return UNITY_END();
}
