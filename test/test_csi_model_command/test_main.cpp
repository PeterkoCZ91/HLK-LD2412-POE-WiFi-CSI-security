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

// #3 (48h audit): IMPORT carries a model payload through the slot so the worker
// (csi_proc) reads it under the state release/acquire fence — no async_tcp race.
void test_import_carries_model_payload_through_slot() {
    CsiModelCommandSlot slot;
    CsiSiteModel m{};
    m.generation = 7;
    m.threshold  = 0.01234f;
    m.valid      = true;
    TEST_ASSERT_TRUE(slot.submit(CsiModelCommand::IMPORT, false, m));

    CsiModelCommand command;
    bool force = true;
    TEST_ASSERT_TRUE(slot.claim(command, force));
    TEST_ASSERT_EQUAL_INT((int)CsiModelCommand::IMPORT, (int)command);
    TEST_ASSERT_FALSE(force);
    // payload readable by the worker between claim() and complete()
    TEST_ASSERT_EQUAL_UINT32(7u, slot.payload().generation);
    TEST_ASSERT_TRUE(slot.payload().valid);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.01234f, slot.payload().threshold);

    slot.complete(CsiModelOp::OK);
    CsiModelOp result = CsiModelOp::STORE_FAILED;
    TEST_ASSERT_TRUE(slot.poll(result));
    TEST_ASSERT_EQUAL_INT((int)CsiModelCommandState::IDLE, (int)slot.state());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_command_runs_in_submit_claim_complete_poll_order);
    RUN_TEST(test_abandon_cancels_an_unclaimed_command);
    RUN_TEST(test_abandoned_executing_command_releases_slot_on_completion);
    RUN_TEST(test_import_carries_model_payload_through_slot);
    return UNITY_END();
}
