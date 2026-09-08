// Unit testy pro MqttCommandRouter (IMPROVEMENTS T6, audit F-01) — pio test -e native
#include <unity.h>
#include "services/MqttCommandRouter.h"

void setUp() {}
void tearDown() {}

// Bez nastaveného PINu je holý příkaz přijat
void test_no_pin_configured_accepts_bare_command() {
    AuthLockout l;
    MqttArmResult r = MqttCommandRouter::evaluateArmCommand("ARM_AWAY", "", l, 1000);
    TEST_ASSERT_TRUE(r.decision == MqttArmDecision::Accepted);
    TEST_ASSERT_TRUE(r.command == MqttArmCommand::ArmAway);
}

void test_no_pin_configured_nullptr_accepts() {
    AuthLockout l;
    MqttArmResult r = MqttCommandRouter::evaluateArmCommand("DISARM", nullptr, l, 1000);
    TEST_ASSERT_TRUE(r.decision == MqttArmDecision::Accepted);
    TEST_ASSERT_TRUE(r.command == MqttArmCommand::Disarm);
}

// Bez nastaveného PINu je "CMD:pin" tvar odmítnut jako neznámý příkaz —
// dvojtečka se nikdy neořízne, protože PIN guard je vypnutý (stejné jako
// původní inline chování: cmdBase == celý payload).
void test_no_pin_configured_colon_shape_is_unknown_command() {
    AuthLockout l;
    MqttArmResult r = MqttCommandRouter::evaluateArmCommand("ARM_AWAY:1234", "", l, 1000);
    TEST_ASSERT_TRUE(r.decision == MqttArmDecision::RejectedUnknownCommand);
}

// Správný PIN -> přijato, lockout streak resetovaný (ověřeno nepřímo: po
// úspěchu potřebuje plný práh k dalšímu zamčení)
void test_correct_pin_accepts_and_clears_streak() {
    AuthLockout l;
    uint32_t t = 1000;
    for (int i = 0; i < AuthLockout::FAIL_THRESHOLD - 1; i++) {
        MqttCommandRouter::evaluateArmCommand("ARM_AWAY:wrong", "1234", l, t += 100);
    }
    MqttArmResult r = MqttCommandRouter::evaluateArmCommand("ARM_HOME:1234", "1234", l, t += 100);
    TEST_ASSERT_TRUE(r.decision == MqttArmDecision::Accepted);
    TEST_ASSERT_TRUE(r.command == MqttArmCommand::ArmHome);
    TEST_ASSERT_EQUAL_UINT32(0, l.lockedForMs(0, t + 1));
}

// Špatný PIN -> odmítnuto, počítá se do lockoutu
void test_wrong_pin_rejected_and_counts_toward_lockout() {
    AuthLockout l;
    uint32_t t = 1000;
    MqttArmResult r;
    for (int i = 0; i < AuthLockout::FAIL_THRESHOLD; i++) {
        r = MqttCommandRouter::evaluateArmCommand("ARM_AWAY:wrong", "1234", l, t += 100);
    }
    TEST_ASSERT_TRUE(r.decision == MqttArmDecision::RejectedWrongPin);
    TEST_ASSERT_TRUE(l.lockedForMs(0, t) > 0);
}

// Holý příkaz (bez ":") při vyžadovaném PINu je odmítnut, ale NESMÍ se počítat
// do lockoutu — jinak by holé ARM/DISARM z Home Assistant ucpalo bucket.
void test_bare_command_rejected_without_touching_lockout() {
    AuthLockout l;
    uint32_t t = 1000;
    MqttArmResult r;
    for (int i = 0; i < 50; i++) {
        r = MqttCommandRouter::evaluateArmCommand("ARM_AWAY", "1234", l, t += 100);
    }
    TEST_ASSERT_TRUE(r.decision == MqttArmDecision::RejectedNoPin);
    TEST_ASSERT_EQUAL_UINT32(0, l.lockedForMs(0, t));
}

// Ve stavu locked-out je příkaz odmítnut BEZ kontroly PINu — i správný PIN
// neprojde, dokud lockout nevyprší.
void test_locked_out_rejects_even_correct_pin() {
    AuthLockout l;
    uint32_t t = 1000;
    for (int i = 0; i < AuthLockout::FAIL_THRESHOLD; i++) {
        MqttCommandRouter::evaluateArmCommand("ARM_AWAY:wrong", "1234", l, t += 100);
    }
    MqttArmResult r = MqttCommandRouter::evaluateArmCommand("ARM_AWAY:1234", "1234", l, t += 10);
    TEST_ASSERT_TRUE(r.decision == MqttArmDecision::RejectedLockedOut);
}

// Neznámý base příkaz i se správným PINem -> RejectedUnknownCommand, žádná
// state change navrhovaná volajícímu (Accepted se nikdy nevrátí).
void test_unknown_command_with_correct_pin() {
    AuthLockout l;
    MqttArmResult r = MqttCommandRouter::evaluateArmCommand("PANIC:1234", "1234", l, 1000);
    TEST_ASSERT_TRUE(r.decision == MqttArmDecision::RejectedUnknownCommand);
}

// Všechny tři platné příkazy s PINem
void test_all_valid_commands_with_pin() {
    AuthLockout l;
    TEST_ASSERT_TRUE(MqttCommandRouter::evaluateArmCommand("ARM_AWAY:1234", "1234", l, 1000).command == MqttArmCommand::ArmAway);
    TEST_ASSERT_TRUE(MqttCommandRouter::evaluateArmCommand("ARM_HOME:1234", "1234", l, 1001).command == MqttArmCommand::ArmHome);
    TEST_ASSERT_TRUE(MqttCommandRouter::evaluateArmCommand("DISARM:1234", "1234", l, 1002).command == MqttArmCommand::Disarm);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_no_pin_configured_accepts_bare_command);
    RUN_TEST(test_no_pin_configured_nullptr_accepts);
    RUN_TEST(test_no_pin_configured_colon_shape_is_unknown_command);
    RUN_TEST(test_correct_pin_accepts_and_clears_streak);
    RUN_TEST(test_wrong_pin_rejected_and_counts_toward_lockout);
    RUN_TEST(test_bare_command_rejected_without_touching_lockout);
    RUN_TEST(test_locked_out_rejects_even_correct_pin);
    RUN_TEST(test_unknown_command_with_correct_pin);
    RUN_TEST(test_all_valid_commands_with_pin);
    return UNITY_END();
}
