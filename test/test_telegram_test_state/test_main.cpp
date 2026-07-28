#include <unity.h>

#include "services/TelegramTestState.h"

void test_state_text_is_stable() {
    TEST_ASSERT_EQUAL_STRING("unknown", telegramTestStateText(TelegramTestState::UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("queued", telegramTestStateText(TelegramTestState::QUEUED));
    TEST_ASSERT_EQUAL_STRING("sending", telegramTestStateText(TelegramTestState::SENDING));
    TEST_ASSERT_EQUAL_STRING("succeeded", telegramTestStateText(TelegramTestState::SUCCEEDED));
    TEST_ASSERT_EQUAL_STRING("failed", telegramTestStateText(TelegramTestState::FAILED));
}

void test_only_final_states_are_terminal() {
    TEST_ASSERT_FALSE(telegramTestStateTerminal(TelegramTestState::UNKNOWN));
    TEST_ASSERT_FALSE(telegramTestStateTerminal(TelegramTestState::QUEUED));
    TEST_ASSERT_FALSE(telegramTestStateTerminal(TelegramTestState::SENDING));
    TEST_ASSERT_TRUE(telegramTestStateTerminal(TelegramTestState::SUCCEEDED));
    TEST_ASSERT_TRUE(telegramTestStateTerminal(TelegramTestState::FAILED));
}

void test_tracker_rejects_concurrent_request_and_reports_transitions() {
    TelegramTestTracker tracker;
    TEST_ASSERT_TRUE(tracker.begin(41));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TelegramTestState::QUEUED),
                            static_cast<uint8_t>(tracker.get(41)));
    TEST_ASSERT_FALSE(tracker.begin(42));
    tracker.markSending(41);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TelegramTestState::SENDING),
                            static_cast<uint8_t>(tracker.get(41)));
    tracker.finish(41, TelegramTestState::SUCCEEDED);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TelegramTestState::SUCCEEDED),
                            static_cast<uint8_t>(tracker.get(41)));
}

void test_tracker_keeps_last_result_while_next_request_runs() {
    TelegramTestTracker tracker;
    TEST_ASSERT_TRUE(tracker.begin(1));
    tracker.finish(1, TelegramTestState::FAILED);
    TEST_ASSERT_TRUE(tracker.begin(2));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TelegramTestState::FAILED),
                            static_cast<uint8_t>(tracker.get(1)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TelegramTestState::QUEUED),
                            static_cast<uint8_t>(tracker.get(2)));
    tracker.cancel(2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TelegramTestState::UNKNOWN),
                            static_cast<uint8_t>(tracker.get(2)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_state_text_is_stable);
    RUN_TEST(test_only_final_states_are_terminal);
    RUN_TEST(test_tracker_rejects_concurrent_request_and_reports_transitions);
    RUN_TEST(test_tracker_keeps_last_result_while_next_request_runs);
    return UNITY_END();
}
