// Native unit tests for the Ethernet default-netif re-assertion policy.
// Pure — no Arduino/lwIP. Run: pio test -e native -f test_eth_default_netif
//
// Context (field, 2026-08-30): the csi7 fix that forces Ethernet back as lwIP's
// default netif exists (CSIService::_restoreEthDefaultNetif) but never fires in
// steady state — its only call sites are boot, a WiFi-scan completion, and a
// branch gated behind "traffic gen NOT running". WiFi STA outranks ETH on
// route_prio, so every WiFi DHCP renewal silently takes the default back and
// nothing puts it right. A node ran 177 h with MQTT pinned to a -71 dBm WiFi
// link, failing to connect (rc=-2) while its Ethernet sat up and healthy.
#include <unity.h>
#include "services/EthDefaultNetifPolicy.h"

void setUp() {}
void tearDown() {}

// Re-asserting costs a TCPIP core lock, so the policy must stay quiet while
// Ethernet already holds the default.
void test_no_action_when_eth_already_default() {
    EthDefaultNetifPolicy policy;
    NetifAction a = policy.evaluate(/*nowMs=*/10000, /*ota=*/false,
                                    /*ethLinkUp=*/true, /*ethHasIp=*/true,
                                    /*ethIsDefault=*/true);
    TEST_ASSERT_EQUAL(NetifAction::None, a);
    TEST_ASSERT_EQUAL_UINT32(0, policy.assertCount());
}

// csi7b: netif_set_default() during an OTA transfer can drop the in-flight
// stream. An OTA outranks a wrong default netif — the correction waits.
void test_never_touches_netif_during_ota() {
    EthDefaultNetifPolicy policy;
    NetifAction a = policy.evaluate(10000, /*ota=*/true,
                                    /*ethLinkUp=*/true, /*ethHasIp=*/true,
                                    /*ethIsDefault=*/false);
    TEST_ASSERT_EQUAL(NetifAction::SkipOta, a);
    TEST_ASSERT_EQUAL_UINT32(0, policy.assertCount());
}

// The link this node sits on is down 13.2 % of the time in 2-6 s bursts. A
// re-assertion attempt that lands inside one of those windows must not set the
// default to an interface with no carrier.
void test_skips_while_eth_link_is_down() {
    EthDefaultNetifPolicy policy;
    NetifAction a = policy.evaluate(10000, false,
                                    /*ethLinkUp=*/false, /*ethHasIp=*/true,
                                    /*ethIsDefault=*/false);
    TEST_ASSERT_EQUAL(NetifAction::SkipLinkDown, a);
    TEST_ASSERT_EQUAL_UINT32(0, policy.assertCount());
}

// update() runs every loop iteration; taking LOCK_TCPIP_CORE() that often is
// not acceptable. If an assertion does not stick (WiFi grabs the default right
// back), the retry has to wait out the interval rather than spin on the lock.
void test_assertion_is_rate_limited_but_retries() {
    EthDefaultNetifPolicy policy;  // default interval 5000 ms
    TEST_ASSERT_EQUAL(NetifAction::Assert,
                      policy.evaluate(10000, false, true, true, false));
    // Still not default a second later — too soon to spend another lock.
    TEST_ASSERT_EQUAL(NetifAction::None,
                      policy.evaluate(11000, false, true, true, false));
    // Once the interval has passed it must try again, not give up.
    TEST_ASSERT_EQUAL(NetifAction::Assert,
                      policy.evaluate(15000, false, true, true, false));
    TEST_ASSERT_EQUAL_UINT32(2, policy.assertCount());
}

// An established TCP socket keeps the route it was opened on, so flipping the
// default netif does NOT move a live MQTT session off the WiFi interface — it
// has to be torn down. The caller reports whether the default actually moved;
// the flag is one-shot so a single move triggers exactly one reconnect.
void test_routing_change_is_reported_once() {
    EthDefaultNetifPolicy policy;
    policy.evaluate(10000, false, true, true, /*ethIsDefault=*/true);
    // ETH stays up throughout — WiFi took the default on its own.
    policy.evaluate(11000, false, true, true, /*ethIsDefault=*/false);
    TEST_ASSERT_EQUAL(NetifAction::Assert,
                      policy.evaluate(16000, false, true, true, false));
    TEST_ASSERT_FALSE(policy.takeRoutingChanged());  // nothing reported yet
    policy.noteAsserted(/*defaultChanged=*/true);
    TEST_ASSERT_TRUE(policy.takeRoutingChanged());
    TEST_ASSERT_FALSE(policy.takeRoutingChanged());  // consumed
}

// A re-assertion that finds Ethernet already default (race with the caller, or
// a redundant call) must not tear down a healthy MQTT session — that would turn
// a no-op into a reconnect storm on a node already short of connectivity.
void test_no_reconnect_when_default_did_not_move() {
    EthDefaultNetifPolicy policy;
    policy.evaluate(10000, false, true, true, false);
    policy.noteAsserted(/*defaultChanged=*/false);
    TEST_ASSERT_FALSE(policy.takeRoutingChanged());
    TEST_ASSERT_EQUAL_UINT32(0, policy.changeCount());
}

// FIELD REGRESSION (dev15, 7 min after flash): every ETH flap drops the
// interface, WiFi inherits the default, the link returns 2-6 s later and the
// re-assertion reported a "real move" — forcing an MQTT reconnect, each one
// republishing 59 discovery entities. Six flaps produced six reconnects in
// 90 seconds; at this node's ~3 200 flaps a day that is 3 200 reconnects
// against dev14's ~20. A brief loss of the default is a flap, not a steal, and
// the live session was never really on WiFi.
void test_brief_default_loss_does_not_force_reconnect() {
    EthDefaultNetifPolicy policy;
    policy.evaluate(10000, false, true, true, /*ethIsDefault=*/true);
    // ETH drops out for 4 s — WiFi holds the default meanwhile.
    policy.evaluate(11000, false, false, true, /*ethIsDefault=*/false);
    // Link back; the policy takes the default again.
    TEST_ASSERT_EQUAL(NetifAction::Assert,
                      policy.evaluate(15000, false, true, true, false));
    policy.noteAsserted(/*defaultChanged=*/true);
    TEST_ASSERT_FALSE(policy.takeRoutingChanged());
}

// A default that WiFi has genuinely held for a while is the case this whole
// policy exists for: the live MQTT session really is pinned to the wrong
// interface and only a reconnect moves it.
void test_sustained_default_loss_does_force_reconnect() {
    EthDefaultNetifPolicy policy;
    policy.evaluate(10000, false, true, true, /*ethIsDefault=*/true);
    policy.evaluate(11000, false, true, true, /*ethIsDefault=*/false);
    TEST_ASSERT_EQUAL(NetifAction::Assert,
                      policy.evaluate(16000, false, true, true, false));
    policy.noteAsserted(true);
    TEST_ASSERT_TRUE(policy.takeRoutingChanged());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_no_action_when_eth_already_default);
    RUN_TEST(test_never_touches_netif_during_ota);
    RUN_TEST(test_skips_while_eth_link_is_down);
    RUN_TEST(test_assertion_is_rate_limited_but_retries);
    RUN_TEST(test_routing_change_is_reported_once);
    RUN_TEST(test_no_reconnect_when_default_did_not_move);
    RUN_TEST(test_brief_default_loss_does_not_force_reconnect);
    RUN_TEST(test_sustained_default_loss_does_force_reconnect);
    return UNITY_END();
}
