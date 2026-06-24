// Unit testy pro AlarmFSM (IMPROVEMENTS T5/T6) — pio test -e native
// Čistý stavový automat alarmu vyextrahovaný ze SecurityMonitoru:
// DISARMED -> ARMING -> ARMED -> PENDING -> TRIGGERED, entry/exit delay,
// trigger timeout + auto-rearm, debounce. Čas je injektovaný (now), žádné HW.
#include <unity.h>
#include "services/AlarmFSM.h"

void setUp() {}
void tearDown() {}

// ---- výchozí stav ----
void test_initial_state_is_disarmed() {
    AlarmFSM f;
    TEST_ASSERT_EQUAL(AlarmState::DISARMED, f.state());
}

// ---- arm() ----
void test_arm_immediate_goes_armed() {
    AlarmFSM f;
    TEST_ASSERT_EQUAL(ArmResult::ARMED_IMMEDIATE, f.arm(true, 1000));
    TEST_ASSERT_EQUAL(AlarmState::ARMED, f.state());
}

void test_arm_delayed_goes_arming() {
    AlarmFSM f;
    TEST_ASSERT_EQUAL(ArmResult::ARMING_STARTED, f.arm(false, 1000));
    TEST_ASSERT_EQUAL(AlarmState::ARMING, f.state());
}

void test_arm_idempotent_when_armed() {
    AlarmFSM f;
    f.arm(true, 1000);
    TEST_ASSERT_EQUAL(ArmResult::IDEMPOTENT, f.arm(true, 2000));
    TEST_ASSERT_EQUAL(AlarmState::ARMED, f.state());
}

void test_arm_idempotent_when_arming() {
    AlarmFSM f;
    f.arm(false, 1000);
    TEST_ASSERT_EQUAL(ArmResult::IDEMPOTENT, f.arm(false, 1500));
    TEST_ASSERT_EQUAL(AlarmState::ARMING, f.state());
}

void test_arm_rejected_when_pending() {
    AlarmFSM f;
    f.debounceFrames = 1;
    f.arm(true, 0);
    f.reportMotion(true, 0, 1000);  // ARMED -> PENDING
    TEST_ASSERT_EQUAL(AlarmState::PENDING, f.state());
    TEST_ASSERT_EQUAL(ArmResult::REJECTED, f.arm(true, 2000));
    TEST_ASSERT_EQUAL(AlarmState::PENDING, f.state());
}

void test_arm_rejected_when_triggered() {
    AlarmFSM f;
    f.debounceFrames = 1;
    f.arm(true, 0);
    f.reportMotion(true, 1, 1000);  // immediate -> TRIGGERED
    TEST_ASSERT_EQUAL(AlarmState::TRIGGERED, f.state());
    TEST_ASSERT_EQUAL(ArmResult::REJECTED, f.arm(true, 2000));
    TEST_ASSERT_EQUAL(AlarmState::TRIGGERED, f.state());
}

// ---- exit delay: ARMING -> ARMED (tick) ----
void test_arming_to_armed_after_exit_delay() {
    AlarmFSM f;
    f.exitDelayMs = 30000;
    f.arm(false, 1000);
    TEST_ASSERT_EQUAL(TickEvent::NONE, f.tick(1000 + 29999));
    TEST_ASSERT_EQUAL(AlarmState::ARMING, f.state());
    TEST_ASSERT_EQUAL(TickEvent::EXIT_DELAY_DONE, f.tick(1000 + 30000));
    TEST_ASSERT_EQUAL(AlarmState::ARMED, f.state());
}

// Scheduled-arm regrese (v5.0.1): delayed arm musí po exit delay dojet do ARMED
void test_scheduled_arm_completes_via_tick() {
    AlarmFSM f;
    f.exitDelayMs = 10000;
    TEST_ASSERT_EQUAL(ArmResult::ARMING_STARTED, f.arm(false, 5000));
    for (uint32_t t = 5000; t < 15000; t += 500) f.tick(t);
    f.tick(15000);
    TEST_ASSERT_EQUAL(AlarmState::ARMED, f.state());
}

// ---- motion -> PENDING/TRIGGERED s debounce ----
void test_motion_below_debounce_no_transition() {
    AlarmFSM f;
    f.debounceFrames = 3;
    f.arm(true, 0);
    TEST_ASSERT_EQUAL(MotionEvent::NONE, f.reportMotion(true, 0, 100));
    TEST_ASSERT_EQUAL(MotionEvent::NONE, f.reportMotion(true, 0, 200));
    TEST_ASSERT_EQUAL(AlarmState::ARMED, f.state());
}

void test_motion_meets_debounce_goes_pending() {
    AlarmFSM f;
    f.debounceFrames = 3;
    f.arm(true, 0);
    f.reportMotion(true, 0, 100);
    f.reportMotion(true, 0, 200);
    TEST_ASSERT_EQUAL(MotionEvent::PENDING_STARTED, f.reportMotion(true, 0, 300));
    TEST_ASSERT_EQUAL(AlarmState::PENDING, f.state());
}

void test_debounce_resets_on_non_qualifying_frame() {
    AlarmFSM f;
    f.debounceFrames = 3;
    f.arm(true, 0);
    f.reportMotion(true, 0, 100);
    f.reportMotion(true, 0, 200);
    f.reportMotion(false, 0, 250);   // reset
    f.reportMotion(true, 0, 300);
    f.reportMotion(true, 0, 400);
    TEST_ASSERT_EQUAL(AlarmState::ARMED, f.state());  // jen 2 po resetu
    TEST_ASSERT_EQUAL(MotionEvent::PENDING_STARTED, f.reportMotion(true, 0, 500));
}

void test_motion_immediate_behavior_goes_triggered() {
    AlarmFSM f;
    f.debounceFrames = 1;
    f.arm(true, 0);
    TEST_ASSERT_EQUAL(MotionEvent::TRIGGERED_IMMEDIATE, f.reportMotion(true, 1, 1000));
    TEST_ASSERT_EQUAL(AlarmState::TRIGGERED, f.state());
}

void test_motion_ignore_behavior_no_transition() {
    AlarmFSM f;
    f.debounceFrames = 1;
    f.arm(true, 0);
    TEST_ASSERT_EQUAL(MotionEvent::IGNORED, f.reportMotion(true, 2, 1000));
    TEST_ASSERT_EQUAL(AlarmState::ARMED, f.state());
}

void test_motion_static_zone_behavior3_goes_pending() {
    AlarmFSM f;
    f.debounceFrames = 1;
    f.arm(true, 0);
    TEST_ASSERT_EQUAL(MotionEvent::PENDING_STARTED, f.reportMotion(true, 3, 1000));
    TEST_ASSERT_EQUAL(AlarmState::PENDING, f.state());
}

void test_motion_ignored_when_not_armed() {
    AlarmFSM f;
    f.debounceFrames = 1;
    // DISARMED
    TEST_ASSERT_EQUAL(MotionEvent::NONE, f.reportMotion(true, 1, 1000));
    TEST_ASSERT_EQUAL(AlarmState::DISARMED, f.state());
    // ARMING
    f.arm(false, 2000);
    TEST_ASSERT_EQUAL(MotionEvent::NONE, f.reportMotion(true, 1, 2100));
    TEST_ASSERT_EQUAL(AlarmState::ARMING, f.state());
}

// ---- entry delay: PENDING -> TRIGGERED (tick) ----
void test_pending_to_triggered_after_entry_delay() {
    AlarmFSM f;
    f.debounceFrames = 1;
    f.entryDelayMs = 30000;
    f.arm(true, 0);
    f.reportMotion(true, 0, 1000);   // PENDING @1000
    TEST_ASSERT_EQUAL(TickEvent::NONE, f.tick(1000 + 29999));
    TEST_ASSERT_EQUAL(AlarmState::PENDING, f.state());
    TEST_ASSERT_EQUAL(TickEvent::ENTRY_EXPIRED_TRIGGERED, f.tick(1000 + 30000));
    TEST_ASSERT_EQUAL(AlarmState::TRIGGERED, f.state());
}

// ---- trigger timeout + auto-rearm (tick) ----
void test_triggered_timeout_autorearm_to_armed() {
    AlarmFSM f;
    f.debounceFrames = 1;
    f.triggerTimeoutMs = 900000;
    f.autoRearm = true;
    f.arm(true, 0);
    f.reportMotion(true, 1, 1000);   // TRIGGERED @1000
    TEST_ASSERT_EQUAL(TickEvent::NONE, f.tick(1000 + 899999));
    TEST_ASSERT_EQUAL(TickEvent::AUTO_REARMED, f.tick(1000 + 900000));
    TEST_ASSERT_EQUAL(AlarmState::ARMED, f.state());
}

void test_triggered_timeout_no_autorearm_to_disarmed() {
    AlarmFSM f;
    f.debounceFrames = 1;
    f.triggerTimeoutMs = 900000;
    f.autoRearm = false;
    f.arm(true, 0);
    f.reportMotion(true, 1, 1000);
    TEST_ASSERT_EQUAL(TickEvent::AUTO_DISARMED, f.tick(1000 + 900000));
    TEST_ASSERT_EQUAL(AlarmState::DISARMED, f.state());
}

void test_triggered_timeout_zero_disables_auto_silence() {
    AlarmFSM f;
    f.debounceFrames = 1;
    f.triggerTimeoutMs = 0;     // vypnuto
    f.arm(true, 0);
    f.reportMotion(true, 1, 1000);
    TEST_ASSERT_EQUAL(TickEvent::NONE, f.tick(1000 + 10000000UL));
    TEST_ASSERT_EQUAL(AlarmState::TRIGGERED, f.state());
}

// ---- disarm() ----
void test_disarm_from_armed_reports_change() {
    AlarmFSM f;
    f.arm(true, 0);
    TEST_ASSERT_TRUE(f.disarm(1000));
    TEST_ASSERT_EQUAL(AlarmState::DISARMED, f.state());
}

void test_disarm_from_disarmed_no_change() {
    AlarmFSM f;
    TEST_ASSERT_FALSE(f.disarm(1000));
    TEST_ASSERT_EQUAL(AlarmState::DISARMED, f.state());
}

void test_disarm_from_triggered_reports_change() {
    AlarmFSM f;
    f.debounceFrames = 1;
    f.arm(true, 0);
    f.reportMotion(true, 1, 1000);
    TEST_ASSERT_TRUE(f.disarm(2000));
    TEST_ASSERT_EQUAL(AlarmState::DISARMED, f.state());
}

// re-arm po disarmu funguje (debounce vyresetován)
void test_rearm_after_disarm_starts_fresh() {
    AlarmFSM f;
    f.debounceFrames = 2;
    f.arm(true, 0);
    f.reportMotion(true, 0, 100);   // 1 frame pak disarm
    f.disarm(200);
    f.arm(true, 300);
    TEST_ASSERT_EQUAL(MotionEvent::NONE, f.reportMotion(true, 0, 400));  // počítadlo od nuly
    TEST_ASSERT_EQUAL(MotionEvent::PENDING_STARTED, f.reportMotion(true, 0, 500));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_disarmed);
    RUN_TEST(test_arm_immediate_goes_armed);
    RUN_TEST(test_arm_delayed_goes_arming);
    RUN_TEST(test_arm_idempotent_when_armed);
    RUN_TEST(test_arm_idempotent_when_arming);
    RUN_TEST(test_arm_rejected_when_pending);
    RUN_TEST(test_arm_rejected_when_triggered);
    RUN_TEST(test_arming_to_armed_after_exit_delay);
    RUN_TEST(test_scheduled_arm_completes_via_tick);
    RUN_TEST(test_motion_below_debounce_no_transition);
    RUN_TEST(test_motion_meets_debounce_goes_pending);
    RUN_TEST(test_debounce_resets_on_non_qualifying_frame);
    RUN_TEST(test_motion_immediate_behavior_goes_triggered);
    RUN_TEST(test_motion_ignore_behavior_no_transition);
    RUN_TEST(test_motion_static_zone_behavior3_goes_pending);
    RUN_TEST(test_motion_ignored_when_not_armed);
    RUN_TEST(test_pending_to_triggered_after_entry_delay);
    RUN_TEST(test_triggered_timeout_autorearm_to_armed);
    RUN_TEST(test_triggered_timeout_no_autorearm_to_disarmed);
    RUN_TEST(test_triggered_timeout_zero_disables_auto_silence);
    RUN_TEST(test_disarm_from_armed_reports_change);
    RUN_TEST(test_disarm_from_disarmed_no_change);
    RUN_TEST(test_disarm_from_triggered_reports_change);
    RUN_TEST(test_rearm_after_disarm_starts_fresh);
    return UNITY_END();
}
