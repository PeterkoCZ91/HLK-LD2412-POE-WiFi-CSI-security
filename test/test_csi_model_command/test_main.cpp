#include <unity.h>
#include "services/CsiModelCommand.h"

void setUp() {}
void tearDown() {}

void test_command_runs_in_submit_claim_complete_poll_order() {
    CsiModelCommandSlot slot;
    TEST_ASSERT_TRUE(slot.submit(CsiModelCommand::APPLY, true));
    TEST_ASSERT_FALSE(slot.submit(CsiModelCommand::ROLLBACK, false));

    CsiModelCommand command;
    bool force = false;
    TEST_ASSERT_TRUE(slot.claim(command, force));
    TEST_ASSERT_EQUAL_INT((int)CsiModelCommand::APPLY, (int)command);
    TEST_ASSERT_TRUE(force);

    slot.complete(CsiModelOp::OK);
    CsiModelOp result = CsiModelOp::STORE_FAILED;
    TEST_ASSERT_TRUE(slot.poll(result));
    TEST_ASSERT_EQUAL_INT((int)CsiModelOp::OK, (int)result);
    TEST_ASSERT_EQUAL_INT((int)CsiModelCommandState::IDLE, (int)slot.state());
}

void test_abandon_cancels_an_unclaimed_command() {
    CsiModelCommandSlot slot;
    TEST_ASSERT_TRUE(slot.submit(CsiModelCommand::CLEAR_ALL, false));
    slot.abandon();

    CsiModelCommand command;
    bool force = false;
    TEST_ASSERT_FALSE(slot.claim(command, force));
    TEST_ASSERT_EQUAL_INT((int)CsiModelCommandState::IDLE, (int)slot.state());
}

void test_abandoned_executing_command_releases_slot_on_completion() {
    CsiModelCommandSlot slot;
    TEST_ASSERT_TRUE(slot.submit(CsiModelCommand::ROLLBACK, false));
    CsiModelCommand command;
    bool force = false;
    TEST_ASSERT_TRUE(slot.claim(command, force));

    slot.abandon();
    slot.complete(CsiModelOp::STORE_FAILED);
    TEST_ASSERT_EQUAL_INT((int)CsiModelCommandState::IDLE, (int)slot.state());
    TEST_ASSERT_TRUE(slot.submit(CsiModelCommand::CLEAR_CANDIDATE, false));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_command_runs_in_submit_claim_complete_poll_order);
    RUN_TEST(test_abandon_cancels_an_unclaimed_command);
    RUN_TEST(test_abandoned_executing_command_releases_slot_on_completion);
    return UNITY_END();
}
