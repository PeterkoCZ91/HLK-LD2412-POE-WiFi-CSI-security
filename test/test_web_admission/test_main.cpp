// Native unit tests for the concurrent-request admission policy.
// Pure — no Arduino/lwIP. Run: pio test -e native -f test_web_admission
//
// Context (bench, dev17, 2026-08-31): N parallel POST/GET /api/health against
// an 8 MB node (3857 B payload, Digest auth) behaved like this:
//
//   N <= 10  no reaction, web_gate.close_count = 0
//   N == 12  gate closed, 2 refused, node survived
//   N == 15  survived once, then "Exception/Panic (oom_gate heap=2040/1012)"
//   N == 20  "Software reset (oom_gate heap=8660/3572)"
//   last forensic line: heapmin 27560->5972 lg=6644 fr=7640
//
// The dev7 heap gate sits in the accept path and is alloc-free, so it was not
// too late — it was too BLIND. A connection costs almost nothing when it is
// accepted; its request object, Digest/header Strings, JsonDocument, ~3.9 kB
// response buffer and lwIP send buffers land milliseconds later, after the
// whole burst has been drained. All 20 accepts therefore read the same healthy
// ~45 kB free and passed. Nothing counted how many connections had already
// been admitted against that one reading.
#include <unity.h>
#include "services/WebAdmissionPolicy.h"
#include "services/HeapGatePolicy.h"

void setUp() {}
void tearDown() {}

static WebAdmissionConfig defaults() {
    // Explicit historical configuration: keep the original dev17 policy cases
    // pinned to the numbers that scenario was measured with, independent of
    // later production recalibrations of the struct's own defaults below.
    WebAdmissionConfig cfg;
    cfg.maxInFlight = 8;
    cfg.reserveBytes = 3u * 1024;
    cfg.floorBytes = 14u * 1024;
    return cfg;
}

// Heap the node sits at when idle, per the bench run.
static const uint32_t HEALTHY_FREE = 45u * 1024;
// Resident cost of one in-flight /api/health, derived from the bench: 12
// concurrent requests took free heap from ~45 kB under the 14 kB close level.
static const uint32_t PER_REQUEST_COST = 2600;

// ---------------------------------------------------------------------------
// The regression the whole change exists for.
// ---------------------------------------------------------------------------

// THE BUG: replay the N=20 burst the way the firmware saw it — the heap reads
// healthy on every accept because nothing has been spent yet. A level check on
// that reading admits all 20. The policy must stop at the cap regardless.
void test_burst_of_20_against_a_still_healthy_heap_stops_at_the_cap() {
    WebAdmissionPolicy policy(defaults());
    HeapGatePolicy gate;  // stays open the whole burst — that is the point

    int accepted = 0;
    for (int i = 0; i < 20; i++) {
        // Same millisecond, same heap reading: the async_tcp task drains the
        // whole accept backlog in one pass.
        bool gateClosed = !gate.shouldAccept(HEALTHY_FREE, 20u * 1024);
        TEST_ASSERT_FALSE(gateClosed);
        if (policy.admit(1000, HEALTHY_FREE, gateClosed).accepted()) accepted++;
    }
    TEST_ASSERT_EQUAL_UINT32(0, gate.closeCount());   // the gate never even noticed
    TEST_ASSERT_EQUAL_INT(8, accepted);
    TEST_ASSERT_EQUAL_UINT8(8, policy.inFlight());
    TEST_ASSERT_EQUAL_UINT32(12, policy.rejectsConcurrency());
}

// The property that matters on hardware: whatever the burst does, the heap the
// admitted requests consume must never approach the panic band — 7640 B free /
// 6644 B largest was the last reading before the coredump.
//
// Reality sits between two models, so pin both. Here the cost of each admitted
// request is already visible when the next one is decided (the optimistic end);
// the reserve rule then tightens further and stops at 6, below the cap of 8.
// The pessimistic end — the heap still reading healthy for the whole burst — is
// the test above, which stops at the cap.
void test_capped_burst_keeps_the_heap_out_of_the_panic_band() {
    WebAdmissionPolicy policy(defaults());
    uint32_t freeHeap = HEALTHY_FREE;
    int accepted = 0;

    for (int i = 0; i < 20; i++) {
        if (policy.admit(1000, freeHeap, /*gateClosed=*/false).accepted()) {
            accepted++;
            freeHeap -= PER_REQUEST_COST;
        }
    }
    TEST_ASSERT_EQUAL_INT(6, accepted);
    // 45 kB - 6 * 2.6 kB = ~29 kB: never even reaches the gate's close level,
    // let alone the ~7.6 kB the node panicked at.
    TEST_ASSERT_TRUE_MESSAGE(freeHeap > 14u * 1024,
        "capped burst must not even reach the heap gate's close threshold");

    // Pre-fix behaviour for contrast: 20 admissions * 2.6 kB = 52 kB > 45 kB —
    // the heap runs out mid-burst, which is exactly what N=15 and N=20 did.
    TEST_ASSERT_TRUE(20u * PER_REQUEST_COST > HEALTHY_FREE);
}

// ---------------------------------------------------------------------------
// Heap headroom — the adaptive half of the rule.
// ---------------------------------------------------------------------------

// The node is rarely idle: CSI, MQTT and ETH flaps run concurrently, which is
// why N=15 killed it once and survived once. When the heap is already low the
// cap must not be the only limit — admission has to stop early.
void test_low_heap_stops_admission_long_before_the_cap() {
    WebAdmissionPolicy policy(defaults());
    // 26 kB free: floor 14 kB + (n+1) * 3 kB  =>  n + 1 <= 4  =>  4 admitted.
    int accepted = 0;
    for (int i = 0; i < 10; i++) {
        if (policy.admit(1000, 26u * 1024, false).accepted()) accepted++;
    }
    TEST_ASSERT_EQUAL_INT(4, accepted);
    TEST_ASSERT_EQUAL_UINT32(6, policy.rejectsReserve());
    TEST_ASSERT_EQUAL_UINT32(0, policy.rejectsConcurrency());
}

// A heap already under the floor admits nobody at all, cap or no cap.
void test_heap_under_the_floor_admits_nothing() {
    WebAdmissionPolicy policy(defaults());
    WebAdmitResult r = policy.admit(1000, 10u * 1024, false);
    TEST_ASSERT_FALSE(r.accepted());
    TEST_ASSERT_EQUAL(WebAdmit::RejectReserve, r.decision);
    TEST_ASSERT_EQUAL_UINT8(0, policy.inFlight());
}

// The gate's close threshold is operator-tunable (NVS wg8_close); the admission
// floor follows it so the two limits cannot drift apart.
void test_floor_tracks_the_heap_gate_close_threshold() {
    WebAdmissionPolicy policy(defaults());
    HeapGateConfig gcfg;
    gcfg.closeFreeBytes = 20u * 1024;  // operator raised the gate
    policy.setFloorBytes(gcfg.closeFreeBytes);
    // 26 kB free now only covers (26-20)/3 = 2 connections.
    int accepted = 0;
    for (int i = 0; i < 10; i++) {
        if (policy.admit(1000, 26u * 1024, false).accepted()) accepted++;
    }
    TEST_ASSERT_EQUAL_INT(2, accepted);
}

// ---------------------------------------------------------------------------
// Composition with the existing heap gate.
// ---------------------------------------------------------------------------

// A closed gate outranks everything and must not burn an admission slot —
// otherwise a gate episode would leak the whole slot table.
void test_closed_gate_rejects_without_taking_a_slot() {
    WebAdmissionPolicy policy(defaults());
    WebAdmitResult r = policy.admit(1000, HEALTHY_FREE, /*gateClosed=*/true);
    TEST_ASSERT_FALSE(r.accepted());
    TEST_ASSERT_EQUAL(WebAdmit::RejectHeapGate, r.decision);
    TEST_ASSERT_EQUAL_UINT16(WebAdmissionPolicy::kNoToken, r.slot);
    TEST_ASSERT_EQUAL_UINT8(0, policy.inFlight());
    TEST_ASSERT_EQUAL_UINT32(0, policy.rejectsConcurrency());
    TEST_ASSERT_EQUAL_UINT32(0, policy.rejectsReserve());
}

// ---------------------------------------------------------------------------
// Normal traffic must keep working — the cap is a burst limit, not a quota.
// ---------------------------------------------------------------------------

// This server closes the connection after every response, so a Digest client
// spends two connections per call (401 challenge, then the authorized one).
// The second wave has to be admitted as the first wave's slots come back.
void test_released_slots_are_reusable_for_the_next_wave() {
    WebAdmissionPolicy policy(defaults());
    uint16_t slots[8];
    for (int i = 0; i < 8; i++) {
        WebAdmitResult r = policy.admit(1000, HEALTHY_FREE, false);
        TEST_ASSERT_TRUE(r.accepted());
        slots[i] = r.slot;
    }
    TEST_ASSERT_FALSE(policy.admit(1000, HEALTHY_FREE, false).accepted());

    for (int i = 0; i < 8; i++) policy.release(slots[i]);
    TEST_ASSERT_EQUAL_UINT8(0, policy.inFlight());
    TEST_ASSERT_EQUAL_UINT32(8, policy.releasedTotal());

    // Digest round two, still well inside the TTL.
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_TRUE(policy.admit(1050, HEALTHY_FREE, false).accepted());
    }
    TEST_ASSERT_EQUAL_UINT8(8, policy.peakInFlight());
}

// A long sequence of ordinary one-at-a-time requests must never be refused.
void test_sequential_traffic_is_never_throttled() {
    WebAdmissionPolicy policy(defaults());
    for (int i = 0; i < 200; i++) {
        uint32_t now = 1000 + (uint32_t)i * 50;
        WebAdmitResult r = policy.admit(now, HEALTHY_FREE, false);
        TEST_ASSERT_TRUE(r.accepted());
        policy.release(r.slot);
    }
    TEST_ASSERT_EQUAL_UINT32(0, policy.rejectsConcurrency());
    TEST_ASSERT_EQUAL_UINT32(0, policy.rejectsReserve());
    TEST_ASSERT_EQUAL_UINT8(1, policy.peakInFlight());
}

// ---------------------------------------------------------------------------
// The completion signal is best-effort — a missed one must not wedge the node.
// ---------------------------------------------------------------------------

// WebRoutes' own OOM guards call request->abort(), which reaches lwIP without
// running the disconnect chain; the notification only comes back as an AsyncTCP
// event packet allocated with new(std::nothrow) — the allocation that fails
// first under heap exhaustion. If those slots never came back, the fix would
// turn a crash into a permanently unreachable web server.
void test_slots_whose_disconnect_never_fires_are_reclaimed() {
    WebAdmissionPolicy policy(defaults());
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_TRUE(policy.admit(1000, HEALTHY_FREE, false).accepted());
    }
    TEST_ASSERT_FALSE(policy.admit(5000, HEALTHY_FREE, false).accepted());  // still inside TTL

    // 10 s later nobody released anything — reclaim and serve again.
    WebAdmitResult r = policy.admit(11000, HEALTHY_FREE, false);
    TEST_ASSERT_TRUE(r.accepted());
    TEST_ASSERT_EQUAL_UINT32(8, policy.slotsExpired());
    TEST_ASSERT_EQUAL_UINT8(1, policy.inFlight());
}

// A connection whose slot the TTL already reclaimed can still disconnect later.
// Its stale slot must not free the slot's new owner.
void test_stale_slot_after_a_ttl_reclaim_frees_nothing() {
    WebAdmissionPolicy policy(defaults());
    WebAdmitResult first = policy.admit(1000, HEALTHY_FREE, false);
    TEST_ASSERT_TRUE(first.accepted());

    // TTL reclaims it, a new connection takes the same slot index.
    WebAdmitResult second = policy.admit(20000, HEALTHY_FREE, false);
    TEST_ASSERT_TRUE(second.accepted());
    TEST_ASSERT_EQUAL_UINT8(1, policy.inFlight());
    TEST_ASSERT_NOT_EQUAL(first.slot, second.slot);  // generation differs

    policy.release(first.slot);  // the zombie finally disconnects
    TEST_ASSERT_EQUAL_UINT8(1, policy.inFlight());
    TEST_ASSERT_EQUAL_UINT32(0, policy.releasedTotal());

    policy.release(second.slot);
    TEST_ASSERT_EQUAL_UINT8(0, policy.inFlight());
}

// Double release / unknown slot / kNoToken are all no-ops. A slot leak here
// would show up as a server that refuses everything after a few hours.
void test_release_is_idempotent_and_ignores_unknown_slots() {
    WebAdmissionPolicy policy(defaults());
    WebAdmitResult r = policy.admit(1000, HEALTHY_FREE, false);
    TEST_ASSERT_TRUE(r.accepted());

    policy.release(r.slot);
    policy.release(r.slot);
    policy.release(WebAdmissionPolicy::kNoToken);
    policy.release(0x0042);  // slot index beyond the table

    TEST_ASSERT_EQUAL_UINT8(0, policy.inFlight());
    TEST_ASSERT_EQUAL_UINT32(1, policy.releasedTotal());
}

// millis() wraps every ~49.7 days. A node that has been up that long must not
// start expiring live slots (or stop expiring dead ones) across the wrap.
void test_ttl_survives_the_millis_wrap() {
    WebAdmissionPolicy policy(defaults());
    const uint32_t nearWrap = 0xFFFFF000u;
    WebAdmitResult r = policy.admit(nearWrap, HEALTHY_FREE, false);
    TEST_ASSERT_TRUE(r.accepted());

    policy.expire(nearWrap + 5000);   // 5 s later, past the wrap
    TEST_ASSERT_EQUAL_UINT8(1, policy.inFlight());
    TEST_ASSERT_EQUAL_UINT32(0, policy.slotsExpired());

    policy.expire(nearWrap + 10001);  // TTL elapsed
    TEST_ASSERT_EQUAL_UINT8(0, policy.inFlight());
    TEST_ASSERT_EQUAL_UINT32(1, policy.slotsExpired());
}

// ---------------------------------------------------------------------------
// Configuration guards.
// ---------------------------------------------------------------------------

// A cap above the slot table would let the counter run ahead of the storage and
// hand out kNoToken on an "accepted" decision.
void test_cap_is_clamped_to_the_slot_table() {
    WebAdmissionConfig cfg = defaults();
    cfg.maxInFlight = 200;
    WebAdmissionPolicy policy(cfg);
    TEST_ASSERT_EQUAL_UINT8(WebAdmissionPolicy::kSlots, policy.config().maxInFlight);

    for (int i = 0; i < WebAdmissionPolicy::kSlots; i++) {
        // Plenty of heap so only the cap can bite.
        TEST_ASSERT_TRUE(policy.admit(1000, 1024u * 1024, false).accepted());
    }
    WebAdmitResult r = policy.admit(1000, 1024u * 1024, false);
    TEST_ASSERT_FALSE(r.accepted());
    TEST_ASSERT_EQUAL(WebAdmit::RejectConcurrency, r.decision);
}

// Disabled, the policy is transparent — no tracking, no rejections. Keeps a
// field escape hatch identical in spirit to HeapGateConfig::enabled.
void test_disabled_policy_accepts_everything() {
    WebAdmissionConfig cfg = defaults();
    cfg.enabled = false;
    WebAdmissionPolicy policy(cfg);
    for (int i = 0; i < 50; i++) {
        TEST_ASSERT_TRUE(policy.admit(1000, 1024, /*gateClosed=*/true).accepted());
    }
    TEST_ASSERT_EQUAL_UINT8(0, policy.inFlight());
    TEST_ASSERT_EQUAL_UINT32(0, policy.rejectsConcurrency());
}

void test_production_reserve_bounds_idf5_burst() {
    // dev6 (2026-09-06): a clean restart with no web load showed free heap
    // settling at ~32 KiB once MQTT connects (was ~45 kB before that connect) —
    // see docs/RELEASE_5.7.1_VALIDATION.md. floorBytes was raised from 14 to
    // 20 KiB to restore real margin at that baseline; recheck this test's
    // expected count if either number changes again.
    WebAdmissionPolicy policy;
    unsigned accepted = 0;
    for (unsigned i = 0; i < 6; ++i)
        accepted += policy.admit(1000, 33032, false).accepted();
    TEST_ASSERT_EQUAL_UINT(1, accepted);
    TEST_ASSERT_TRUE(33032 - accepted * 8192 >= policy.config().floorBytes);
}

void test_production_cap_and_recovery() {
    WebAdmissionPolicy policy;
    WebAdmitResult slots[4];
    for (auto& slot : slots) {
        slot = policy.admit(1000, 100000, false);
        TEST_ASSERT_TRUE(slot.accepted());
    }
    TEST_ASSERT_FALSE(policy.admit(1000, 100000, false).accepted());
    for (auto& slot : slots) policy.release(slot.slot);
    TEST_ASSERT_TRUE(policy.admit(1001, 33032, false).accepted());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_production_reserve_bounds_idf5_burst);
    RUN_TEST(test_production_cap_and_recovery);
    RUN_TEST(test_burst_of_20_against_a_still_healthy_heap_stops_at_the_cap);
    RUN_TEST(test_capped_burst_keeps_the_heap_out_of_the_panic_band);
    RUN_TEST(test_low_heap_stops_admission_long_before_the_cap);
    RUN_TEST(test_heap_under_the_floor_admits_nothing);
    RUN_TEST(test_floor_tracks_the_heap_gate_close_threshold);
    RUN_TEST(test_closed_gate_rejects_without_taking_a_slot);
    RUN_TEST(test_released_slots_are_reusable_for_the_next_wave);
    RUN_TEST(test_sequential_traffic_is_never_throttled);
    RUN_TEST(test_slots_whose_disconnect_never_fires_are_reclaimed);
    RUN_TEST(test_stale_slot_after_a_ttl_reclaim_frees_nothing);
    RUN_TEST(test_release_is_idempotent_and_ignores_unknown_slots);
    RUN_TEST(test_ttl_survives_the_millis_wrap);
    RUN_TEST(test_cap_is_clamped_to_the_slot_table);
    RUN_TEST(test_disabled_policy_accepts_everything);
    return UNITY_END();
}
