# Changelog

All notable changes to this project will be documented in this file.

## [5.7.0] - 2026-09-04

Reliability release, composed from dev1 through dev19 on top of the v5.6.0
base. The line opened as a detection-tuning change — the selectable adaptive
percentile (#13) — and turned into memory and networking forensics after a
field node spent three days in a panic streak that nobody noticed until its
`reset_history` was read by hand. Almost every entry below was found on
hardware rather than in review: a decoded coredump, a packet capture on the
LAN segment, or a soak that ran long enough to contradict an assumption made
earlier in the same line. Two entries correct earlier fixes from this very
line; both are kept in the history rather than squashed away, because the
reason each one failed is the useful part.

The lab node closed the dev1-dev17 line at 177.54 h without a reboot, against
64.14 h for the best build before it. dev18/dev19's admission and
skip-visibility hardening closed its own two-node soak at 42.5 h, zero
reboots on either node, the web gate never closing and zero rejected
requests; the lab node's MQTT-reconnect rate (9.6/day) held stable against an
independent measurement at less than half that runtime. Native coverage rises
from 240 to 385 tests, and all five shipped firmware environments build.

### Added

- **The device can now be asked how much memory it really has.**
  `/api/health` publishes a `heap` object carrying both allocator capabilities
  side by side (`free_8bit`, `largest_8bit`, `min_free_8bit`, `free_internal`,
  `largest_internal`, `unusable_internal`, `internal_misleading`), an `sse`
  object (`clients`, `avg_waiting`, `backlog_skips`) for the send-queue depth
  that drained the heap unnoticed for weeks, and — from dev17 — an on-device
  heap watermark tripwire that writes one forensic line into the
  RTC-mirrored log whenever the low-water mark drops by 2 kB or more. The
  common thread is that each of the three failures in this release was
  invisible from outside the node, and in the tripwire's case measurably
  faster than any HTTP poll that could have observed it.
- **Two gates that keep a collapsing heap from becoming a panic (dev7).** A
  web accept gate refuses new connections with an allocation-free TCP RST
  while free heap or the largest allocatable block is below threshold, capping
  concurrent SSE clients at 4 on the way; a `std::set_new_handler` last resort
  stamps an RTC-noinit marker and performs a controlled `esp_restart()` when a
  throwing `new` cannot be satisfied anywhere in the firmware, so the next
  boot reports cause `oom_gate heap=<free>/<largest>` instead of a bare panic
  loop. Both were confirmed working in the field during this line: a load test
  drove the node to `largest` 5876 B against the 6144 B close threshold, the
  gate closed, and the restart was orderly.
- **The node reports its own outages (dev8).** A dirty boot — panic, watchdog,
  brownout or `oom_gate` restart — now produces a Telegram message and a
  non-retained MQTT event on `security/<id>/system/outage` carrying the reset
  reason, uptime before the outage, the pre-crash heap triple and whether a
  coredump is waiting. A survived heap-pressure episode is reported when the
  web gate reopens, because at close time the heap cannot afford a TLS
  handshake. Clean boots stay quiet.
- **CSI passage-edge events (dev6).** Motion transitions are published as a
  compact non-retained edge on `<mqtt_id>/csi/event` with `boot_id` and `seq`,
  so Home Assistant can act on a passage instead of on a retained state topic
  that pins `ON` and re-fires after every HA restart.
  `POST /api/csi/selftest/edge` emits a synthetic, source-marked edge so the
  wire and the automation can be validated without physical motion.
- **Measured Ethernet link health and routing (dev14, dev15).**
  `ethernet.flap{}` in `/api/health`, `eth_flaps` / `eth_down_permille` in
  `/healthz` and three `poe2412_eth_*` series in `/metrics` count link
  episodes off the driver's own events instead of a once-a-minute poll;
  `ethernet.route{eth_is_default, asserts, changes}` reports which interface
  lwIP hands outbound TCP to. Until this release the only way to answer that
  second question was a packet capture on the LAN segment.
- **The node now says how often it drops a publish cycle, and how deep the
  heap went when it did (dev19).** `loop()` skips the entire MQTT publish
  block — all three tiers — and the SSE telemetry tick whenever free heap is
  under `HEAP_MIN_FOR_PUBLISH`. Until now the only trace was a bare
  `[WARN] Low heap — skipping MQTT publish` carrying no number and
  rate-limited to one line per 10 s, so ten lines over a 37 h soak could
  equally have been ten events or ten thousand. `/api/health` gains a
  `heap_skips` object (`mqtt`, `mqtt_logged`, `sse`, `lowest_free`) that
  counts every skip regardless of the rate limit, and the serial line now
  reports the free heap it actually decided on, plus `largest`, `min`,
  in-flight HTTP requests, SSE depth and the runtime operation.
- **Selectable adaptive-threshold percentile (#13, dev1).** The rolling
  detection threshold was hard-wired to the P95 of the idle-variance window
  and is now settable to P99, which rides higher on the noise tail and cuts
  false positives on a noisy link at the cost of sensitivity. Set via
  `POST /api/csi?adaptive_pct=95|99`, persisted as `csi_adapt_pct`, reported
  as `adaptive_percentile`.

### Fixed

- **Every heap threshold in the firmware was inflated by roughly 42 kB
  (dev12).** arduino-esp32 implements `ESP.getFreeHeap()` and friends over
  `MALLOC_CAP_INTERNAL`, which on this board also counts a 40948 B IRAM-only
  region that `malloc()` and `operator new` can never allocate from. The
  practical effect was that the dev7 web gate could not close: a production
  node logged nine `oom_gate` restarts in 6.3 h with `close_count: 0`,
  `free_internal` never below ~43 kB and `largest_internal` pinned at exactly
  40948 in all nine markers. Every heap decision now reads the
  byte-addressable heap, and the gate, `HEAP_MIN_FOR_PUBLISH`, the low-RAM
  alerts, the security health check and the TLS admission policy were all
  recalibrated to real bytes.
- **A single stalled dashboard tab could exhaust the heap (dev13).**
  `AsyncEventSource` bounds its per-client queue by message count, not bytes,
  and the cap meant to hold it at 8 sat in a `build_flags` list that every
  board env replaces rather than extends — so the shipped firmware ran the
  library default of 32 the whole time. At a ~1.5 kB telemetry payload every
  250 ms that is ~48 kB pinned in separately allocated strings, more than the
  entire free heap on this board. A tick is now dropped rather than queued
  behind one that never went out.
- **A burst of concurrent requests could panic the node (dev18).** The
  low-heap gate sits on the accept path and allocates nothing, which is
  correct, but it is a level check on a lagging signal: accepting a connection
  is nearly free, while its request object, Digest strings and ~3.9 kB
  response buffer are paid milliseconds later. Fifteen parallel authenticated
  requests were therefore all admitted against one healthy reading and then ran
  the heap to 2 kB — a panic rather than a controlled restart. Admission now
  bounds connections in flight and charges each one a reserve against the
  gate's own threshold, so it stops admitting long before the ceiling when the
  node is already under CSI, MQTT or link-flap pressure.
- **The heap tripwire stored its evidence where link events evicted it
  (dev18).** It wrote through the system log into a 20-slot RTC ring shared
  with ordinary logging; on a flapping link that ring turns over in about 50
  minutes. It now owns a dedicated ring, readable at `GET
  /api/heap/watermarks`, with the previous boot's records preserved across a
  panic.
- **Out-of-memory recovery paths that themselves allocated (dev6).** Four web
  handlers answered a failed `new (std::nothrow)` with `request->send(503)`,
  which allocates through a throwing `new`; under real exhaustion the second
  allocation also fails, `bad_alloc` cannot be constructed, and
  `std::terminate()` reboots the node. The OOM branches now use the
  allocation-free `request->abort()`, and a source-invariant test keeps the
  property from regressing.
- **MQTT was leaving over the CSI capture radio instead of Ethernet (dev15,
  dev16).** On this dual-homed hardware both interfaces sit on one flat
  subnet and the WiFi STA outranks Ethernet on `route_prio`, so lwIP hands the
  default netif to WiFi at every DHCP renewal. The existing csi7 correction
  had three call sites, none of which fire on a node that is simply up and
  working. A node ran 177 h with MQTT pinned to a -71 dBm link, losing the
  broker about 20 times a day for minutes at a time while its Ethernet was up
  and healthy — with the alarm armed, those are gaps in alert delivery. A
  policy now re-asserts Ethernet periodically and forces a reconnect only when
  a live socket is genuinely stranded, which took two attempts to get right
  (see dev15 and dev16 below).
- **The Ethernet watchdog could reboot a node whose link works (dev14).** It
  summed "time down" across unrelated outages on a flapping link, so six
  unlucky consecutive samples reached the 5 minute reboot threshold. It now
  detects that the link came back in between and restarts the timer; a
  genuinely dead link still reboots on the same rule.
- **Thirteen false tamper alerts, from two independent defects (dev14).** The
  detector was sampled once a minute but fed an instantaneous one-second
  packet rate, so two unlucky dips 60 s apart looked like a minute of
  blindness; and Ethernet comes up seconds after reset while the WiFi station
  needs far longer to associate, so a fresh node reported itself sabotaged
  once per boot. The detector now derives the average over the real interval
  between calls and a bounded startup grace covers the association window. A
  sensor that never delivers a packet is still reported — this is deliberately
  not gated on association, because covering the access point is the attack
  being watched for.
- **One producer's alerts could silence another's (dev14).** The notification
  cooldown was keyed by type alone while three call sites share `TAMPER_ALERT`
  and four share `HEALTH_WARNING`, so a false CSI tamper muted a genuine radar
  tamper for five minutes. The key is now `(type, AlertSource)`.
- **A runtime traffic-generator change could panic the node (dev9).** The web
  setters run on `async_tcp` but the generator task is owned by `loopTask`;
  restarting it from the wrong task while holding the lwIP core lock could
  orphan the old task and spawn a second generator, and two of them contending
  the lock starved `loopTask` past its watchdog. The setters now stage the
  change and `update()` performs it on the owning task.
- **Reconnect and offline behaviour around the broker (dev10, dev13).**
  Offline persistence keeps event and alarm messages only, so ordinary
  telemetry no longer writes to LittleFS during an outage, and replay is
  batched so recovery cannot monopolize `loopTask`. PubSubClient's keepalive
  and socket timeout are now set explicitly (60 s and 4 s): both library
  defaults are 15 s, and because `readByte()` busy-waits for its full
  duration, one truncated packet parked `loopTask` for 15 s — long enough to
  starve CSI and to miss the very keepalive that then tore the connection
  down.
- **The CSI threshold path could enter MOTION on frozen data (dev7).** At
  pps=0 the turbulence buffer and running variance hold their last values, and
  roughly 8 s of starvation accumulated enough smoothing votes to fire an
  alarm on a production node. A tick without fresh packets now freezes the
  whole decision, reports a `data_starved` reason and counts into
  `csi.starved_ticks`; the ML path had had such a gate since v5.4, the
  threshold path had none.
- **`radar_task` is no longer started on units where no radar answers
  (dev13)**, where it spun every 2 ms to do nothing while holding an 8192 B
  stack, and boot-outage notices now name the reporting firmware version
  (dev11).
- **Review residua from the v5.6.0 + #13 diffs (dev2).** `adaptive_pct` is
  parsed strictly instead of through `toFloat()`, whose 0-on-failure was
  clamped into a persisted P50 — the exact opposite of the desensitization
  intent; `csi_adapt_pct` joined config export/import so a restored backup no
  longer silently reverts a P99 node to P95; the SSE buffer grew to 2048 B
  before the fusion block could push a frame past the cap and freeze the whole
  dashboard; config-import rollback reports restore failures instead of
  logging a clean rollback that did not happen.

### Changed

- **Heap figures reported by the device changed meaning (dev12).**
  `free_heap` and `min_heap` in `/api/health`, `heap_free`/`heap_min`/
  `heap_largest` in `/metrics` and `/healthz`, and the `free_heap` /
  `max_alloc_heap` MQTT topics now carry byte-addressable bytes. History
  recorded before this release reads roughly 42 kB higher for the same
  physical state on this board; dashboards and alert thresholds built on the
  old numbers need re-basing. The web gate's NVS keys were renamed to
  `wg8_*` rather than migrated, because the old values were calibrated
  against inflated readings and would have held the gate permanently closed.
- **The MQTT topic `security/<id>/eth_link` is no longer called
  `security/<id>/rssi` (dev14).** It has only ever carried the Ethernet link
  state as `ON`/`OFF` and no RSSI value is published over MQTT at all. The
  Home Assistant discovery `uniq_id` is unchanged, so entities and history are
  preserved; the retained message under the old name stays on the broker until
  cleared by hand.

### Known issues

- **A node whose Ethernet port flaps is a site problem, not a firmware one.**
  The lab node measured 24071 episodes over 7.4 days, 13.2 % of wall-clock
  down, a 2 s median and a clean geometric length distribution on the PHY's
  2000 ms poll grid, while a broker on the same segment lost no ICMP at all.
  The firmware now survives this (no watchdog reboots, no reconnect storms)
  and reports it honestly, but the cable, port or injector is what fixes it.

## [5.7.0-dev19] - 2026-09-02

### Added

- **The node now says how often it drops a publish cycle, and how deep the
  heap went when it did.** `loop()` skips the entire MQTT publish block — all
  three tiers — and the SSE telemetry tick whenever free heap is under
  `HEAP_MIN_FOR_PUBLISH`. Until now the only trace was a bare
  `[WARN] Low heap — skipping MQTT publish` carrying no number and rate-limited
  to one line per 10 s, so ten lines over a 37 h soak could equally have been
  ten events or ten thousand. `/api/health` gains a `heap_skips` object
  (`mqtt`, `mqtt_logged`, `sse`, `lowest_free`) that counts every skip
  regardless of the rate limit, and the serial line now reports the free heap
  it actually decided on, plus `largest`, `min`, in-flight HTTP requests, SSE
  depth and the runtime operation. `in_flight` is the point of the exercise: it
  says whether an AsyncTCP request was being served at the instant `loop()`
  found the heap collapsed.

### Changed

- The low-heap guard reads `heapFreeUsable()` once and reports that same
  reading. Re-reading it for the log line would have printed a heap that had
  already recovered — which is how this dip stayed invisible for a week.

Field evidence (bench node, 37 h on dev18): ten skip warnings while the `STAB`
heartbeat five seconds either side reported 59 180 B free against a 12 000 B
threshold. Both figures come from the same `heapFreeUsable()` call inside the
same `loop()` iteration with no `return` between them, so the heap genuinely
loses ~47 kB within one pass and hands it straight back. The watermark ring
missed the corresponding 2 680 → 1 976 B step because 704 B is below its 2 kB
recording threshold; the threshold is deliberately unchanged, since the ring
holds 16 slots and lowering it would fill them all in the first minute after
boot. Nine native tests added (385 total).

## [5.7.0-dev18] - 2026-08-31

### Fixed

- A burst of concurrent authenticated requests could panic the node. The dev7
  low-heap gate runs on the accept path and is allocation-free, which is the
  right place — but it is a level check on a lagging signal. Accepting a
  connection costs almost nothing; its request object, header and Digest
  strings, `JsonDocument` and ~3.9 kB response buffer are paid milliseconds
  later, after async_tcp has drained the whole accept backlog in one pass. So
  fifteen or twenty parallel requests were all admitted against a single
  healthy ~45 kB reading, and nothing counted how much had already been
  committed against it. On the bench ten concurrent requests moved the
  watermark by 216 B, twelve closed the gate and were survived, and fifteen
  survived once and then panicked at `heap=2040/1012`. `WebAdmissionPolicy`
  now bounds how many connections may be in flight (8, against a browser's
  six sockets per host) and charges each admitted connection a 3 kB reserve
  against the gate's own close threshold, so under CSI, MQTT or link-flap
  pressure admission stops well before the ceiling. Slots carry a 10 s TTL
  because two paths never report a disconnect — an SSE stream takes the raw
  client's callback, and `request->abort()` reports through an AsyncTCP event
  packet allocated with `new (std::nothrow)`, the allocation that fails first
  under the exhaustion being guarded. Counters are in `/api/health` →
  `web_gate.admission{}`.
- The heap watermark tripwire wrote its findings through the system log into
  the 20-slot RTC ring shared with ordinary logging. On a node whose Ethernet
  port flaps, that ring turns over in roughly 50 minutes: a check three hours
  after deployment found all 20 slots holding link events, and the five lines
  showing HA discovery draining 33 kB of watermark across its 59 entities were
  already gone. A forensic instrument cannot keep its evidence where a
  chattier producer can evict it, so the tripwire now owns a dedicated 16-slot
  RTC ring that nothing else writes to, readable at `GET
  /api/heap/watermarks`, with the previous boot's records copied into RAM at
  startup so they survive a panic.

## [5.7.0-dev17] - 2026-08-31

### Added

- **A tripwire that records when the heap low-water mark moved and what was
  running at that moment.** The 12 h dev16 soak ended with `min_free_8bit` at
  7700 B against a 48200 B median — 6.6 kB below the web gate's close
  threshold and inside the band where this node has historically OOMed — yet
  not one of 2488 samples taken every 15 s saw the heap below 29504 B. Whoever
  takes those ~40 kB takes them and gives them back inside a single sampling
  period, and sampling faster from outside is self-defeating: an
  `/api/health` request allocates more than the spike being hunted.
  `heap_caps_get_minimum_free_size()` already holds the minimum; what was
  missing was a timestamp and a context. The board therefore polls its own
  watermark every 100 ms in `loop()` and, on each drop, writes one line into
  the system log — mirrored into the RTC-noinit ring, so it survives a panic —
  carrying the previous and new watermark, the largest block, free heap, MQTT
  connected state and reconnect count, the Home Assistant discovery index, the
  running `RuntimeOperation` and RSSI. The shape of the policy is dictated by
  the ring holding 20 slots shared with ordinary logging: an instrument that
  fired on every 200 B ratchet would evict the evidence it exists to collect.
  Hence a minimum drop of 2 kB measured against the last *recorded* level, so
  a slow ratchet of sub-threshold steps still adds up to one honest line
  rather than being discarded step by step; a hard cap of 8 lines per boot;
  and a rising watermark treated as a stale baseline that re-anchors quietly,
  never as a negative drop. Pure header, 8 native tests, 355/355 in total.

The instrument earned its keep on the first day, and two of its findings
change conclusions recorded earlier in this line. Home Assistant discovery had
been ruled out as the source of the deep dips, correctly on the question that
was asked — `publishDiscoveryStep()` sends one entity per `update()` cycle and
allocates single-digit kB per entity, so it cannot take 40 kB in one jump. But
the watermark does not answer "how much did anyone take at once", it answers
"how deep did this get in total", and across 59 entities the discovery
sequence drove it from 56316 B down to 22940 B — 33 kB — inside 17 s of boot.
That reframes the dev16 dip as a discovery republish landing on an already
loaded heap rather than one exotic allocation. Separately, a load test pushed
the node to `largest` 5876 B while 15120 B were free: a 9.2 kB gap where every
other measurement in this line shows a constant ~3 kB, which is the one
instance so far of genuine fragmentation rather than a merely full heap. The
block size was below the gate's 6144 B threshold, the gate closed as designed,
and `oom_gate` performed a controlled restart — the dev7 and dev12 chain
verified in the field rather than in a test.

Two limitations are known and deliberately left for the next build, since
fixing either costs a reboot and restarts the soak: there is no boot grace
period, so the startup transient spends 2 of the 8 slots, and the shared ring
turns over in roughly 50 minutes on a node with a flapping link.

## [5.7.0-dev16] - 2026-08-31

### Fixed

- **An Ethernet flap is not a routing steal, and must not force an MQTT
  reconnect.** The dev15 correction below regressed in the field seven minutes
  after the flash: re-asserting Ethernet as the default netif worked, but the
  reconnect that follows a real move fired on every link flap. Measured over
  the first 15 minutes: 27 flaps, 27 asserts, 27 changes and 28 reconnects —
  one for one. On this node's ~3200 flaps a day that projects to ~2600
  reconnects against dev14's ~20, each one republishing 59 Home Assistant
  discovery entities. The correction was worse than the fault it corrected.
  The cause is that "the default moved" conflated two different events. When
  the Ethernet carrier drops, lwIP hands the default to WiFi and hands it
  straight back seconds later; any Ethernet-borne session broke on its own and
  a reconnect adds nothing. When Ethernet stays up throughout and WiFi takes
  the default anyway — a DHCP renewal, the higher `route_prio` — the live
  session really is stranded on the wrong interface, and only then is the
  reconnect worth its cost. The policy now records whether the carrier was
  down at any point while the default was lost, and reports a routing change
  only when it was not. A dwell-time threshold was tried first and rejected on
  its own logic: the policy re-asserts every 5 s, so the default is never lost
  long enough for any threshold to fire and the reconnect would simply never
  happen — the sampling period of the observer being mistaken for the duration
  of the event, the same trap as the Ethernet flap counter in dev14, from the
  other side.

Verified against dev15 at the same point after boot, because the flap rate
takes about seven minutes to settle: at 7 minutes dev15 had 6 flaps and 7
reconnects, dev16 had 14 flaps and 1; over 15 minutes, 27 flaps / 28
reconnects against 34 flaps / 1. `changes` keeps rising in both, so the
default really is moving and the policy really is putting it back — what is
gone is the link from a flap to a reconnect.

A 12 h soak answered the question those 15 minutes could not, compared against
the last 12 h of the dev14 run rather than its 177 h average: 3032 flaps/day
and 19.9 reconnects/day for dev16, against 2585 and 30.0 for dev14 — a third
fewer reconnects on 17 % more flaps, and two orders of magnitude away from
dev15. `eth_is_default` held `true` in 1248 of 1249 samples. The unplanned
benefit was in delivery latency: `mq_age`, the time since the last MQTT
traffic, peaked at 614 s under dev14 — over ten minutes during which nothing
could reach Home Assistant — against 121 s under dev16. That follows directly
from dev15's premise, since dev14 was sending over a starved capture radio and
dev16 sends over Ethernet, and it is the part of this fix that matters for an
armed node.

## [5.7.0-dev15] - 2026-08-31

### Fixed

- **Ethernet is kept as the outbound default, so MQTT stops riding the CSI
  radio.** This node is dual-homed with both interfaces on one flat subnet:
  Ethernet for transport and a WiFi station for CSI capture. The station
  outranks Ethernet on `esp_netif` `route_prio`, so lwIP hands the default
  netif to WiFi every time that interface takes an IP. The csi7 fix already
  addressed this once, and its comment describes this exact case — but its
  correction never runs in steady state: `_restoreEthDefaultNetif()` is called
  from exactly three places, namely boot, the completion of a WiFi scan, and a
  branch gated behind "traffic generator NOT running". None of the three fire
  on a node that is simply up and working, so the first DHCP renewal on the
  capture interface takes the default and keeps it. Measured in the field: a
  node ran 177 h with MQTT pinned to a -71 dBm link, cycling `rc=-2` at
  moments when its Ethernet was up and healthy, and losing the broker roughly
  20 times a day for minutes at a time — nearly 8 minutes in the worst
  observed episode. A packet capture on the LAN segment settled it: 30 of 30
  packets to the broker left from the WiFi address while a concurrent HTTP
  request left over Ethernet, so this is a real per-socket routing choice and
  not a dead interface. With the alarm armed, those windows are gaps in alert
  delivery — the security path riding the same weak radio that the sensor path
  is already starving on. `EthDefaultNetifPolicy` (pure header, 6 native
  tests, TDD) decides when to re-assert: it stays quiet while Ethernet already
  holds the default, so the core lock is not touched for nothing; it never
  goes near lwIP during an OTA transfer (csi7b); it skips while the carrier is
  down without latching off, because on a link that is down 13 % of the time
  it has to keep trying; and it rate-limits retries to 5 s so `update()`
  cannot spin on `LOCK_TCPIP_CORE()`. It is called from `update()` on the loop
  task, which is the only safe place — the lock is not reentrant, so a WiFi or
  lwIP event callback would deadlock. The second half matters as much as the
  first: an established TCP socket keeps the route it was opened on, so moving
  the default does not migrate a live MQTT session. `_restoreEthDefaultNetif()`
  therefore reports whether the default actually moved, and only a real move
  forces a reconnect.

  **This build shipped a regression**, corrected in dev16 above: the forced
  reconnect fired on every Ethernet flap, at roughly 2600 reconnects a day.
  Nothing in the test suite caught it — a 15 minute monitor sampling every two
  minutes right after the flash did, because it showed `changes` and
  `mqtt.reconnect_total` next to each other.

### Added

- `/api/health` reports `ethernet.route{eth_is_default, asserts, changes}`.
  Until now the only way to answer "which interface does outbound TCP use" was
  a packet capture on the LAN segment, and the answer turned out to be the
  wrong one for 177 h without anything on the device saying so.

## [5.7.0-dev14] - 2026-08-22

### Added

- The Ethernet link's real flap rate is now measured instead of inferred.
  `ARDUINO_EVENT_ETH_{CONNECTED,DISCONNECTED}` already fired at the true rate
  and nothing counted them, so every consumer had to guess from a poll: the
  connectivity watchdog samples `ETH.linkUp()` once a minute and printed
  "restored after 60s" for a 2 s glitch, and MQTT diagnostics publish every
  30 s, which made Home Assistant's history show 339 episodes/day at a 30 s
  median. Measured off the driver's own edges, the same link was losing 18
  episodes per 230 s. Flapping is bursty, so across windows the same link
  measures 10-24 % of wall-clock down and ~2400-6800 episodes/day — an order of
  magnitude away from the reported figure at either end of the range — with
  every single episode a whole multiple of the IDF PHY poll period (2000 ms). Exposed as
  `ethernet.flap{}` in `/api/health`, `eth_flaps` / `eth_down_permille` in
  `/healthz`, and `poe2412_eth_flaps_total` / `_down_seconds_total` /
  `_down_permille` in `/metrics`. The watchdog log line now says how long the
  link was *unseen* and quotes the measured total beside it, so the poll period
  can no longer be misread as an outage length.

### Fixed

- One producer's alerts could silence a different producer's. The notification
  cooldown was keyed by `NotificationType` alone, but several unrelated call
  sites share a type: three raise `TAMPER_ALERT` (radar tamper, CSI tamper,
  anti-masking) and four raise `HEALTH_WARNING` (disarm reminder,
  sensor-silent, cross-modal desync, low memory). A false CSI tamper therefore
  muted a *genuine* radar tamper for the full five minutes, and the chattiest
  health producer masked the other three. The project had hit this before and
  treated it as a naming problem — the "FIX #18" comment in SecurityMonitor.cpp
  moved a zone alert off `TAMPER_ALERT` for exactly this reason, which
  relocated the collision into `HEALTH_WARNING` rather than removing it. The
  cooldown is now keyed by `(type, AlertSource)`, with every repeating producer
  naming itself; anything still passing `GENERIC` keeps the old per-type slot,
  so nothing that was rate-limited before becomes unlimited.
- The Ethernet watchdog could reboot a node whose link works. It accumulated
  "time down" from the first 60 s poll that saw the link down and only reset
  when a poll saw it up, so on a flapping link it summed unrelated outages: at
  the 23.5 % down fraction measured in the field, six consecutive unlucky
  samples is roughly a daily event, and six of them is the 5 minute reboot
  threshold. It now compares the driver's outage counter across samples — if a
  new outage began since the last one, the link came back in between and is
  flapping rather than dead, so the timer restarts. A genuinely dead link still
  reboots on the same 5 minute rule, including when no driver events arrive at
  all.
- CSI tamper detection raised 13 false "sensor tamper" alerts on a field node,
  from two independent defects. **Aliasing:** `SecurityMonitor` samples the
  detector once per 60 s (`INTERVAL_HEALTH_CHECK_MS`) but fed it
  `getPacketRate()`, an instantaneous 1-second average — two unlucky dips 60 s
  apart were indistinguishable from 60 s of genuine blindness. The detector now
  takes the monotonic packet count and derives the average over the real
  interval between calls, so the verdict no longer depends on the caller's
  cadence, and a trickle too slow to see the room still counts as blind.
  **Boot transient:** Ethernet comes up seconds after reset while the WiFi
  station needs far longer to associate, so a fresh node reported itself
  sabotaged once per boot — which is why all 13 alerts landed either just after
  a boot or during the OOM crash-loop era, and none while the node ran
  undisturbed. A bounded startup grace covers the window until the first packet
  arrives; a sensor that never delivers one is still reported. The threat model
  is unchanged: this is deliberately not gated on WiFi association, because
  pulling or covering the AP disassociates the station and that is the attack
  being watched for.
- `CSIService::isActive()` is latched true at startup and never cleared, so the
  tamper detector could not tell "blinded" from "deliberately idle" and read a
  WiFi scan or an in-flight OTA as sabotage. It now consults `isSensing()`,
  which also accounts for scan suspension and OTA.
- The MQTT topic `security/<id>/eth_link` is no longer called
  `security/<id>/rssi`. It has only ever carried the Ethernet link state as
  `ON`/`OFF`; no RSSI value is published over MQTT at all, so the old name
  suggested a signal that does not exist to anyone reading the broker. The
  Home Assistant discovery `uniq_id` is unchanged, so the existing entity and
  its history are preserved with no user action. Note that the retained
  `security/<id>/rssi` message stays on the broker until cleared by hand
  (`mosquitto_pub -r -n -t security/<id>/rssi`); the firmware has no code path
  for deleting retained topics.
- The Home Assistant entity list in the README advertised a `sensor.<id>_rssi`
  that has never existed, alongside two other entity names that did not match
  the published discovery configs.

## [5.7.0-dev13] - 2026-08-20

### Fixed

- `-D SSE_MAX_QUEUED_MESSAGES=8` never reached the compiler. It sat in
  `[common].build_flags`, but every board env does `extends = common` and then
  redefines `build_flags`, and PlatformIO REPLACES an inherited list instead of
  appending to it. The shipped firmware therefore ran the library default of 32
  the whole time, despite a source comment describing exactly the failure mode
  this cap was meant to prevent. Confirmed inert with a `static_assert` probe
  and moved into the env that actually builds.
- An undrained SSE client could exhaust the heap. `AsyncEventSource` bounds its
  per-client queue by MESSAGE COUNT, not by bytes (the default 32 was in force
  because of the dead flag above), and this firmware pushes a ~1.5 kB telemetry
  payload every
  250 ms. One dashboard tab whose ACKs stalled therefore pinned up to
  32 x 1.5 kB = ~48 kB in separately allocated `String`s — more than the whole
  byte-addressable free heap on this board (25-38 kB measured). The field log
  caught it exactly: `gate CLOSED free=10800 largest=5876 sse=1` followed by
  `oom_gate heap=916/148`. The dev7 accept gate cannot help here, because the
  offending client is already connected. A dashboard only ever wants the newest
  reading, so a tick is now dropped rather than queued behind one that never
  went out (`WEB_SSE_MAX_BACKLOG`, default 3 outstanding packets).
- `radar_task` is no longer started on units where no radar answers. On such a
  unit `LD2412Service::update()` returns immediately on `!_radar`, so the task
  spun every 2 ms taking and releasing a mutex to do nothing while holding an
  8192 B stack. Creation is now idempotent and lazy, so a radar that only
  answers later (the post-OTA restore path re-begins it) still gets its task;
  radar-equipped units are unaffected.
- PubSubClient keepalive and socket timeout are now set explicitly (60 s and
  4 s). Both library defaults are 15 s and neither was ever overridden, so
  bounded `loopTask` stalls from CSI work read as a dead broker: the library
  silently dropped the socket and the reconnect replayed all 59 Home Assistant
  discovery entities. A production node logged four such reconnects within five
  minutes of boot settling. The 15 s socket timeout was the worse half —
  `readByte()` busy-waits on `yield()` for its full duration, so one truncated
  packet parked `loopTask` for 15 s, long enough to starve CSI and to miss the
  very keepalive that then tore the connection down. Detection of a genuinely
  dead broker is unaffected: publishes fail fast on `!connected()`.

### Added

- `/api/health` reports an `sse` object (`clients`, `avg_waiting`,
  `backlog_skips`). SSE depth was previously invisible from outside the device,
  which is why a client draining the heap went unnoticed for so long.

## [5.7.0-dev12] - 2026-08-20

### Fixed

- Every heap decision now reads the byte-addressable heap (`MALLOC_CAP_8BIT`)
  instead of `MALLOC_CAP_INTERNAL`. arduino-esp32 implements
  `ESP.getFreeHeap()`/`getMaxAllocHeap()`/`getMinFreeHeap()` over
  `MALLOC_CAP_INTERNAL`, which also counts the leftover IRAM-only heap region
  (0x40096000-0x400A0000 on this build: 40960 B raw, 40948 B usable). That
  region carries `MALLOC_CAP_EXEC|MALLOC_CAP_32BIT` but not `MALLOC_CAP_8BIT`,
  so `malloc()` and `operator new` can never allocate from it. Every threshold
  in the firmware was consequently inflated by ~42 kB on this hardware.
- The dev7 web low-heap gate could not close. A production node logged nine
  `oom_gate` restarts in 6.3 h with `close_count: 0` and `rejects_total: 0`:
  `free_internal` never fell below ~43 kB and `largest_internal` was pinned at
  exactly 40948 in all nine markers, against 28 kB/12 kB close thresholds. The
  L1/L1b/L2 layers were unreachable code; only the L3 restart ever fired.
- Gate thresholds recalibrated to usable-heap bytes: close at 14 kB free /
  6 kB largest, reopen above 22 kB / 10 kB. The node's real working band is
  ~36-45 kB free, and the field OOMs struck between 1.4 and 13 kB, so the gate
  now engages above that range instead of never.
- `HEAP_MIN_FOR_PUBLISH` (SSE and MQTT publish suppression), the Telegram
  low-RAM alerts, the security health check and the TLS handshake admission
  policy were all comparing thresholds against the inflated number and could
  not trigger either. They now see the real heap; the TLS policy is
  correspondingly stricter and may refuse a handshake where it previously
  passed on memory that did not exist.
- The `oom_gate` marker records usable-heap figures, so `reset_history` no
  longer reports tens of kB free at the instant a throwing `new` failed.
- Remaining heap thresholds re-expressed in usable bytes, since honest readings
  put several of them out of reach: `HEAP_MIN_FOR_PUBLISH` 20000 -> 12000,
  `HEAP_LOW_WARNING` 30000 -> 18000, `HEAP_WARN_BYTES` 40000 -> 24000,
  `HEAP_CRIT_BYTES` 20000 -> 12000, `HEAP_RECOVER_BYTES` 60000 -> 32000. The
  recovery threshold mattered most: above the board's real ceiling, a low-RAM
  alert could never have cleared. TLS admission moved to 36000/14000, sized on
  an mbedTLS handshake's actual peak rather than on inflated readings.

### Changed

- `free_heap` and `min_heap` in `/api/health`, `heap_free`/`heap_min`/
  `heap_largest` in `/metrics` and `/healthz`, and the `free_heap` /
  `max_alloc_heap` MQTT topics now report the byte-addressable heap. Recorded
  history from before dev12 reads roughly 42 kB higher for the same physical
  state on this board.
- `/api/health` gained a `heap` object publishing both capabilities side by
  side (`free_8bit`, `largest_8bit`, `min_free_8bit`, `free_internal`,
  `largest_internal`, `unusable_internal`, `internal_misleading`) so this class
  of bug is visible from the outside on any future hardware.
- Gate threshold NVS keys renamed to `wg8_close`, `wg8_open`, `wg8_lclose`,
  `wg8_lopen`. The dev7 keys held values calibrated against the inflated
  readings; reusing them would have held the gate permanently closed against
  honest numbers, so they are abandoned rather than migrated.

## [5.7.0-dev11] - 2026-08-20

### Fixed

- Boot-outage notifications now include the reporting firmware version in
  Telegram text and the machine-readable MQTT event payload.

## [5.7.0-dev10] - 2026-08-20

### Fixed

- MQTT offline persistence now keeps only event/alarm messages, avoiding
  LittleFS writes for ordinary telemetry while the broker is unavailable.
- Offline replay is limited to small batches, so reconnect recovery cannot
  monopolize `loopTask` or trigger the Task WDT after a prolonged outage.

## [5.7.0-dev9] - 2026-08-18

### Fixed

- **Runtime traffic-generator config change no longer crashes the node
  (Task WDT panic).** Changing the CSI traffic generator at runtime via the web
  API (`traffic_icmp` / `traffic_port` / `traffic_pps`) could reboot the device
  on a Task-WDT panic (field 2026-08-18). Root cause: the setters run on the
  `async_tcp` task, but the generator task is owned by `loopTask` (`update()`
  starts it). Restarting it from `async_tcp` — a blocking `_stopTrafficGen()`
  wait plus a respawn while `async_tcp` holds the lwIP TCPIP core lock — could
  orphan the old task (its handle was nulled after a 1 s timeout even when the
  task was still tearing down) and spawn a **second** generator; two tasks
  contending the TCPIP core lock starved `loopTask` past its 60 s watchdog.
  Fix mirrors the model-command-slot pattern: the setters now only stage the
  new value and raise a restart flag; `update()` performs the stop/start on
  `loopTask`, the task owner, where it is safe. Hardened `_stopTrafficGen()` to
  clear the handle only once the task actually reached `eDeleted`, and
  `_startTrafficGen()` to refuse a start while a previous task is still alive —
  so a second generator can never be spawned even under the race. (Concurrency/
  task-placement fix — verified by build + on-device soak, not unit-testable.)

## [5.7.0-dev8] - 2026-08-17

The node now reports its own outages. A three-day panic streak earlier this
month went unnoticed until reset_history was read by hand — everything the
device knows about a crash at the next boot is now pushed over Telegram
instead of waiting to be audited.

### Added

- **Boot outage Telegram notice.** After a dirty boot — panic, any watchdog,
  brownout, or an `oom_gate` restart (a controlled `esp_restart()`, so only
  the cause string carries the incident) — the node sends a Telegram message
  with the reset reason, restart cause, uptime before the outage, the
  pre-crash heap triple recorded by `safeRestart()`/the OOM marker, and
  whether a coredump is waiting. Clean boots stay quiet: OTA and user
  restarts are intentional, and power-on can't be told apart from a routine
  smart-plug power-cycle on PoE sites. The message is built in `setup()` from
  reset-history data (pure `BootOutageNotice.h`, +`test_boot_outage`, 9
  tests) and delivered by a loop one-shot once MQTT is up, with 30 s retries
  (Telegram TLS may lag) and a 5-attempt cap.
- **Heap-pressure episode Telegram notice.** When the dev7 web heap gate
  reopens, the node reports the survived episode — closed duration, total
  rejected connections, current/minimum heap. Sent on REOPEN, not close: at
  close time the heap cannot afford a Telegram TLS handshake (the existing
  TLS memory gate would veto it anyway).
- **MQTT outage events (`security/<id>/system/outage`).** Both incidents above
  are also published as **non-retained** JSON events (`boot_outage`,
  `heap_gate_episode`) so a Home Assistant automation can forward them to
  Telegram — HA-side Telegram is the deployment norm, the node-side bot is
  often unconfigured. Non-retained on purpose: an HA restart must not replay
  a stale outage (same rationale as the dev6 passage edge). The retained
  `system/restart_cause` topic is unchanged and still fires on every boot.

## [5.7.0-dev7] - 2026-08-17

Two independent gates closing the two failure classes surfaced by the
2026-08-15 field coredump and the 2026-08-17 armed false trigger: the web
server stops taking new work while the heap is collapsing (instead of
panicking inside library code), and the CSI threshold path stops voting on
frozen data while the capture is packet-starved.

### Added

- **Web low-heap accept gate (L1).** The 2026-08-15 coredump showed the dev6
  OOM guards working — and the panic simply moving into ESPAsyncWebServer's
  own header parsing, which allocates with a *throwing* `new` on a dead heap.
  `GatedAsyncWebServer` re-registers the library's accept callback (no fork;
  the pinned commit's lambda is mirrored with a `new (std::nothrow)`) and
  consults a pure hysteresis policy (`HeapGatePolicy.h`,
  +`test_heap_gate_policy`): the gate CLOSES when free heap < 28 kB or the
  largest allocatable block < 12 kB, rejects new connections with an
  allocation-free TCP RST, and only REOPENS above 40 kB / 16 kB so a heap
  hovering at the boundary cannot flap it. Existing connections are untouched;
  MQTT and the alarm core keep their heap headroom. NVS: `web_gate_en`
  (default 1), `web_gate_close` / `web_gate_open` (kB). State in
  `/api/health` → `web_gate{enabled,closed,rejects_total,close_count}`; the
  loop task probes the gate once a second and logs CLOSE (with a heap/SSE/MQTT
  snapshot, mirrored into the RTC log ring) and reopen transitions.
- **SSE client cap (L1b).** SSE reconnect storms (browser tabs + HA right
  after a link flap — the pattern in the coredump) each hold an AsyncTCP
  connection and send queue. New event streams are refused while the heap
  gate is closed, and concurrent SSE clients are capped at
  `WEB_SSE_MAX_CLIENTS` (4).
- **OOM last resort: controlled `oom_gate` restart instead of a panic loop
  (L3).** `std::set_new_handler` now catches the moment a throwing `new`
  cannot be satisfied anywhere in the firmware. The handler is allocation-free:
  it stamps an RTC-noinit marker (uptime + free heap + largest block,
  checksummed — `OomGuard.h`, +`test_oom_marker`) and calls `esp_restart()`.
  The next boot folds the marker into `reset_history` as cause
  `oom_gate heap=<free>/<largest>` with the marker's uptime. Guards: NVS
  `oom_restart_en=0` restores the old panic+coredump behaviour for debugging;
  an active OTA reboot-inhibit aborts instead (a restart mid-flash-write risks
  a brick); and if the *previous* boot already ended in an `oom_gate` restart
  and the next OOM hits within 60 s, the handler aborts so a restart loop
  surfaces as a visible panic instead of cycling silently.
- **Lab-only OOM stressor.** `POST /api/dev/oom` (compiled only with
  `-D DEV_STRESS`, never in release envs) exhausts the heap on demand to
  bench-validate the whole `oom_gate` → RTC marker → `reset_history` path.

### Fixed

- **CSI threshold path can no longer enter MOTION from packet-starved
  (frozen) data.** Armed false trigger 2026-08-17 12:32 on the production
  node: at pps=0 the turbulence buffer and running variance freeze at their
  last values, `_updateMotionState()` kept comparing the frozen variance
  against the effective threshold every tick, and ~8 s of starvation
  accumulated enough smoothing votes to fire `motion_enter` → alarm. The ML
  path has had a starvation gate since v5.4 (`csiMlVoteTrusted`); the
  threshold path had none. A tick without fresh packets
  (`csiVarianceVoteTrusted`, floor 0.5 pps — any real data passes, a
  zero-packet tick never does) now freezes the whole decision: no smoothing
  shift, no state change, no shadow evaluation, no idle-baseline drift.
  Health events still run, the decision trace reports the new
  `data_starved` reason (plus `data_starved` + `packet_rate` fields in
  `/api/csi/decision`), and `/api/health` counts frozen ticks in
  `csi.starved_ticks`. +3 classifier tests, +3 vote-gate tests.

## [5.7.0-dev6] - 2026-08-13

Passage-edge MQTT events so Home Assistant can detect a real passage from a
discrete edge instead of the pinned retained state, plus a web-handler
out-of-memory fix surfaced by a field coredump.

### Added

- **CSI passage-edge events (`<mqtt_id>/csi/event`).** On every variance-based
  motion transition the node now publishes a compact, **non-retained** JSON
  edge, so Home Assistant can detect a real passage from the EDGE instead of
  the retained `<mqtt_id>/csi/motion` state — which pins `ON`, is replayed to
  every new subscriber, and re-fires after an HA restart. Fields: `v` (schema
  version = 1), `boot_id` (random hex per boot), `seq` (monotonic per boot),
  `event` (`motion_started` / `motion_ended`), `uptime_ms`, `source`
  (`csi_variance` for a real edge), `variance`, `threshold`. `boot_id` +
  `uptime_ms` let a consumer reject a retained / offline-buffer replay from a
  previous boot. The payload formatter is a pure, header-only helper
  (`CsiMotionEdge.h`), so it is native-testable without Arduino/MQTT
  (+`test_csi_motion_edge`). The edge is best-effort: if the broker is down at
  the transition the event is dropped rather than retried, so a stale replayed
  edge can never fake a passage.
- **`POST /api/csi/selftest/edge?state=1|0`.** Emits a synthetic passage edge
  (`source:"selftest"`) on `<mqtt_id>/csi/event`, so the event wire and an HA
  passage automation can be validated deterministically without physical
  motion. `state=1` → `motion_started` (default), `state=0` → `motion_ended`;
  Digest auth like the other mutating CSI endpoints. The `selftest` source
  marking means a synthetic edge can never be mistaken for a real passage.

### Fixed

- **Web-handler OOM recovery no longer panics the `async_tcp` task.** Four
  handlers in `src/WebRoutes.cpp` recovered from a failed `new (std::nothrow)`
  buffer allocation by calling `request->send(503, ...)` — which itself
  allocates a response object through a *throwing* `operator new`. Under real
  heap exhaustion that second allocation also fails, libstdc++ cannot construct
  the `bad_alloc`, and `std::terminate()` reboots the node (field coredump
  2026-08-12). The OOM branches now recover with the allocation-free
  `request->abort()`. A source-invariant guard test (`test_web_alloc_guard`)
  enforces that every `new (std::nothrow)` failure path in `WebRoutes.cpp`
  stays allocation-free, so a regression that reintroduces an allocating
  OOM-recovery branch fails the suite.

## [5.7.0-dev2] - 2026-08-05

Four MED + six LOW findings from the 2026-08-05 surface review of the
v5.6.0 + #13 diffs.

### Fixed

- **`adaptive_pct` API input is now parsed strictly.** The handler used Arduino
  `toFloat()`, which returns 0 for anything unparseable (empty value, decimal
  comma, typo); the setter clamp then turned that 0 into a **persisted P50** —
  dropping the detection threshold to the median of the idle-variance window on
  an armed node, the exact opposite of the P99 desensitization intent. Invalid
  or out-of-band input is now rejected and ignored, like the neighbouring
  `hysteresis` handler (`csiParseAdaptivePercentile`, +2 native tests).
- **`csi_adapt_pct` included in config export/import.** The percentile was
  persisted in NVS but missing from `/api/config/export` and from the import
  validation + write lists, so restoring a backup onto a replacement node
  silently reverted a P99-tuned node to P95. Import validates the [0.50, 0.999]
  band like every other float field.
- **SSE telemetry buffer 1536 → 2048 B.** The v5.6.0 fusion block (~240 B)
  pushed the worst-case frame (eng-mode gate arrays + CSI with ML + learning +
  fusion) to ~1.55 KB, within a few dozen bytes of the cap. On overflow
  `serializeJson` truncates, the length guard rejects the frame, and **every**
  SSE event is silently dropped — the whole dashboard freezes until eng
  mode/learning stops (the pre-v4.1.3 failure mode). Stale sizing comment
  refreshed.
- **Config-import rollback reports restore failures.** `rollback()` discarded
  every restore return value while the handler unconditionally logged "rolled
  back to pre-import state" — under a nearly-full NVS the durable half-state
  that #5 exists to prevent could persist with a falsely clean log. Rollback now
  returns the count of unrestored keys and the handler logs an ERROR telling the
  operator to verify settings before reboot.

### Fixed (LOW residua, same review)

- **Fusion reason no longer claims a CSI vote that never happened.** The CSI
  starvation fallback rendered "radar only, CSI/ML disagree" while CSI had
  simply stopped delivering frames. New `FUSION_CSI_STALE` source bit → "radar
  only (CSI stale, no data)" (+3 native tests).
- **Stale EMA persist can no longer overwrite a just-applied/rolled-back
  model** — `_switchDetectionToActive()` drops the pending main-loop EMA stash
  derived from the previous active slot.
- **Config-import journal reserves its storage up front** — a mid-import
  `push_back` realloc under heap exhaustion would abort (`-fno-exceptions`)
  after keys were written but before rollback could run.
- **Serial RSSI diag uses the shared placement thresholds**
  (`CSI_RSSI_HOT_DBM`/`CSI_RSSI_WEAK_DBM`, was hard-coded -40/-70 predating the
  v5.6.0 tune) and stays silent when not associated (rssi==0), so it cannot
  contradict `/api/health` during live placement tuning.
- **Fusion panel UI:** the radar N/A hatch on radar-less nodes now actually
  renders (inline height beat the `.fbar.na` stylesheet rule), and the panel
  stays hidden until the first SSE frame carries fusion data (no permanently
  dash-filled card when CSI is disabled).

## [5.7.0-dev1] - 2026-07-31

First feature of the v5.7 line, on top of the v5.6 base.

### Added

- **Selectable adaptive-threshold percentile (#13).** The rolling adaptive
  detection threshold was hard-wired to the P95 of the idle-variance window. It is
  now configurable — P95 (the sensitive default) or P99, which rides higher on the
  noise tail and cuts false positives on noisy links at the cost of sensitivity.
  Set via `POST /api/csi?adaptive_pct=95|99` (a fraction like `0.99` or a whole
  percent like `99`; clamped to [0.50, 0.999]), persisted in NVS as `csi_adapt_pct`,
  and reported as `adaptive_percentile` in the `/api/health` csi block. The quantile math is a
  pure, host-tested helper (`CsiAdaptiveThreshold.h`, +5 native tests).

## [5.6.0] - 2026-08-01

Placement-awareness, fusion explainability and a cluster of concurrency and
persistence fixes, composed on top of the 5.5 fusion-liveness base. Validated
across a multi-day dual-node soak (32 h+ armed on the production node, zero
triggers/crashes). Native coverage increases from 228 to 240 tests; all five
shipped firmware environments verified.

### Added

- **Sensor-placement health warnings.** CSI health now reports `rssi_too_strong`
  when the node sits so close to the access point that the strong signal
  saturates detection (empirically confirmed at ~40 cm / around -52 dBm), and
  `rssi_too_weak` when the link is too faint to carry usable CSI. The RSSI sweet
  spot is tuned to `CSI_RSSI_HOT_DBM` -55 / `CSI_RSSI_WEAK_DBM` -70. These are
  passive health reasons — they never change alarm decisions.
- **Live fusion explainability panel (#8).** A new dashboard card shows
  per-modality contribution bars (radar / CSI / ML), a fusion-confidence gauge
  and a plain-language reason string ("radar + CSI + ML agree (95%)"). SSE
  telemetry is enriched with per-modality source bits and bar levels.

### Fixed

- **CSI model-import race.** A web-triggered model import wrote the candidate
  slot directly from the async_tcp task while `applyCandidate` read it on
  csi_proc, so a concurrent import + apply could seal a torn candidate as the
  ACTIVE model. Import now routes through the bounded model-command slot as a new
  `IMPORT` command, serialized on csi_proc with APPLY under a release/acquire
  fence. The web handler submits and polls exactly like apply (BUSY → 409,
  timeout → 503).
- **Continuous-EMA / model-apply cross-task race.** The idle-baseline EMA drift
  is computed on the main loop but used to write the ACTIVE model slot, which the
  `csi_proc` worker also writes during apply/rollback/import. The unsynchronized
  cross-task write could tear `_active` or make an apply's read-back verify see a
  half-written slot and spuriously report `STORE_FAILED` (HTTP 500). The main loop
  now stashes the drifted values and raises a flag; the `csi_proc` worker performs
  the actual slot write, serialized with the model-command path — no cross-task
  write/write on `_active`.
- **Config-import atomicity.** `/api/config/import` wrote NVS key-by-key and, on a
  mid-sequence write failure, left the keys already written in place — able to
  persist e.g. a new `auth_pass` without its `auth_user` and lock the device out
  on the next reboot. The writer now journals each changed key's prior value and,
  on any failure, rolls every changed key back to its pre-import state (removing
  keys that did not exist before). Best-effort under a genuinely full NVS.

## [5.5.0] - 2026-07-28

Fusion-liveness and Home Assistant explainability release candidate. The new
health path is passive by default, while the alarm-timing corroboration gate is
explicitly opt-in. Native coverage increases from 213 to 228 tests.

### Added

- **Cross-modal liveness detector** compares sustained radar and CSI activity
  over a rolling window and reports `crossmodal_desync` plus a health warning
  when one live modality persistently fails to corroborate the other. It never
  changes alarm decisions and defaults enabled.
- **Low-confidence corroboration window** holds motion below 0.6 confidence for
  up to 8 seconds while waiting for a second fusion-source bit. It is an opt-in
  gate before `AlarmFSM::reportMotion`; the FSM itself is unchanged.
- **Home Assistant alarm explanation** publishes retained JSON on
  `security/<device>/alarm/why`, including the reason, radar/CSI/ML contributors,
  fusion confidence and zone. Discovery exposes the reason and JSON attributes.
- **Security sensitivity presets** (`Empty`, `Home`, `Paranoid`) map to tested
  alarm-energy, entry-delay, pet-immunity and corroboration settings. A preset is
  a **baseline** that any explicitly configured per-parameter value overrides, and
  it is only applied when the user has actually selected one — devices that never
  chose a preset keep their own persisted/default tunables (no silent change to
  alarm timing on upgrade). The current preset persists in NVS and is available
  through `/api/security_preset`, an HA select, and `security/<device>/preset[/set]`
  MQTT topics.
- Runtime config keys `crossmodal_enabled`, `corroboration_enabled`, and
  `corroboration_window_ms` are exposed through `/api/security/config`.

## [5.4.1-poe-wifi] - 2026-07-27

Crash-forensics + reliability-first release. Core-dump read-out over the API
turned three previously undiagnosable field crashes into root-caused fixes
within a day; on top of that, this release lands the full reliability-first
roadmap — model-lifecycle correctness and disarm-PIN hardening (Release A), a
single runtime operation coordinator with unified HTTP/MQTT/HA status
(Release B), and security hardening: log/export secret redaction plus
per-service TLS trust management (Release C). Validated across a multi-day lab
soak. All items ship with native tests (213/213 passing).

### Added

- **Nearby WiFi scan in the CSI dashboard and API**: `POST
  /api/csi/wifi/scan` starts an asynchronous scan and `GET` polls up to 20
  unique SSIDs sorted by signal strength, with channel and security details.
  CSI capture is suspended while the radio hops channels, and scans are blocked
  during OTA, calibration and site learning so detector/model data cannot be
  contaminated. Selecting a result fills the runtime WiFi configuration form.
  A retained `security/<device>/csi/wifi_scan` status now reports scan state,
  capture activity, duration, result count and typed failures; HA auto-discovery
  adds diagnostic scan-state and CSI-capture entities. Neighbor SSIDs remain
  local to the authenticated HTTP API and are never published over MQTT.
- **Core dump read-out API**: `GET /api/coredump` (summary: crashed task,
  exception PC), `GET /api/coredump/download` (raw ELF dump for
  `espcoredump.py info_corefile -t raw`), `POST /api/coredump/erase`;
  `coredump_present` flag in `/api/health`. The coredump flash partition and
  the panic-time writer were already in place — only readout was missing.
- **System log persistence across panic/software reset** (RTC noinit RAM
  mirror with CRC validation, pure `LogRing.h` core + native tests).
  Restored entries carry `"prev": true` in `/api/logs`.
- **ML saturation guard** (`CsiMlSaturationGuard`): the MLP ships with
  foreign-site weights/scaler and on some links pins `ml_probability` ≈ 1.0
  around the clock, silently defeating the fusion's radar false-positive
  suppression (which requires CSI *and* ML to disagree). A per-minute
  duty-cycle watchdog distrusts the vote after ≥95 % motion-duty over 6 h
  (recovers after 1 h under 50 %). New health reason `ml_saturated`,
  `ml_duty_pct` in `/api/health`.

### Fixed

- **`bad_alloc` crash serving large JSON** (`async_tcp` abort after ~2.5
  days uptime, root-caused from the first field core dump):
  `AsyncResponseStream` grows a contiguous buffer while writing;
  `/api/csi/events` and `/api/events` now serialize into one
  exactly-measured nothrow allocation and answer `503 Low memory` instead
  of crashing on a fragmented heap.
- **Task-watchdog panic in the WiFi task**: the whole CSI pipeline ran
  per-packet inside the WiFi driver's RX callback (core 0) and could starve
  IDLE0. The callback now only copies the frame into a queue; a dedicated
  `csi_proc` task (core 1) does the processing. `queue_drops` counter in
  `/api/health`.
- **Null-pointer crash on radar-less nodes**: the one-shot gate-config
  verification (40 s after boot) drove the LD2412 UART handshake even when
  radar init had failed (`LoadProhibited` in `getAckNonBlocking`). Skipped
  when the radar is not connected.
- **Dead Man's Switch restart loop**: the 3-restart cap + degraded mode
  never engaged because the restart counter reset seconds after every boot
  (`_lastPublish` is initialized to `millis()`, so "publish not stale" holds
  without any real publish). The counter now resets only after a genuine
  publish since boot (`publishedSinceBoot`), ending the endless ~31.5 min
  reboot cycle during broker outages.
- `/api/coredump` registered with an exact URI matcher so it does not
  shadow `/api/coredump/download`.
- **Pull OTA deploy helper false success**: use preemptive Basic authentication
  for the body-buffering Pull OTA endpoint, reject an empty acceptance response,
  and keep waiting while `/api/version` still reports the pre-reboot firmware
  instead of failing immediately or trusting a stale persisted OTA phase.
- **Offline alarms could be evicted by telemetry, and stalled operations
  never timed out.** A buffered alarm event now has eviction priority over
  routine telemetry in the offline MQTT ring (a long broker outage during an
  intrusion can no longer drop the retained `alarm/event`), and the operation
  coordinator's calibration / site-learning / WiFi-scan timeout poll is ticked
  unconditionally, so a stuck claim self-releases instead of requiring a
  reboot.
- **Buffered alarm events are de-duplicated** so a reconnect replay cannot
  deliver the same alarm twice.
- **HTTP OTA upload request ownership is isolated** — a second concurrent
  upload can no longer stomp the in-flight image; rejected uploads are refused
  up front.
- **CSI model operations apply at frame boundaries and publish coherent
  detection snapshots**, so an apply/rollback/clear cannot be observed
  mid-frame or produce a torn detection read.
- **Blocking network operations moved off the async web handler.** The
  Telegram connectivity test and Pull OTA no longer block the AsyncWebServer
  task (they enqueue and the client polls status), preventing web-server
  stalls and watchdog resets.
- **Ethernet link up/down events are traced** in diagnostics to help
  root-cause dual-home / link-flap OTA failures.
- **CSI WiFi controls repaired**: the AP switch posts credentials in the
  request body, the Security tab no longer crashes on removed RSSI fields, and
  the Czech section labels are restored.

### Reliability & security hardening (Release A)

Batch from an independent reliability-first roadmap review: model-lifecycle
correctness and disarm-PIN hardening, layered on top of the crash-forensics
work above. All items ship with native tests (176/176 passing).

- **`clear_model` now erases the active model, not just legacy keys.** The
  handler called a legacy-only clear that never touched the
  active/candidate/previous slots in `CsiModelManager`, so the API answered
  `200 "cleared"` while the model driving detection survived in NVS and was
  reloaded on the next boot — the only working reset was a hardware
  factory-reset. `factoryClear()` now wipes all three slots (including
  `previous`, so a rollback cannot resurrect a cleared model) and reports
  `STORE_FAILED` instead of a false OK when an NVS erase fails. Detection
  stays fail-safe after a clear (falls back to the configured/relative-floor
  threshold, never a blind zero).
- **Model rollback could half-swap across a reboot.** `rollback()` wrote the
  ACTIVE slot first, so a failure writing PREVIOUS left a half-rolled-back
  active in NVS that survived the next boot despite the call returning
  `STORE_FAILED`. ACTIVE is now written last (matching `apply()`), and new
  failure-injection + reboot-recovery tests assert state after a simulated
  `loadFromStore`, not just RAM.
- **Candidate model apply is gated on AP (BSSID) compatibility.**
  `applyCandidateModel()` blocks an INCOMPATIBLE candidate (`HTTP 409`,
  `force=1` to override); `/api/csi/site_model` reports `ap_compat` for the
  active and candidate slots. Fail-safe: the gate is on *apply* only —
  live detection keeps running, and an UNKNOWN/legacy BSSID does not block.

### Security (Release A)

- **Disarm PIN and MQTT password moved out of GET query strings into the
  authenticated POST body** (`hasParam(name, true)`); the frontend `api()`
  helper gained an `inBody` path. HW-verified on the bench (query `?pin=`
  → `400`, body → `200`).
- **MQTT disarm PIN is now rate-limited** by reusing the existing
  `AuthLockout` (5 failures/60 s → 30 s lockout, doubling). Only PIN-bearing
  ARM/DISARM commands count; bare commands and local detection/siren are
  untouched.
- **MQTT payloads are no longer logged verbatim.** `handleMessage()` logged
  the raw payload — including `CMD:<pin>` — into the 4 KB debug ring buffer
  readable over `GET /api/debug` even in non-debug builds. It now logs the
  topic and payload length only, closing an auth-gated disarm-PIN leak.
- **HA `alarm_control_panel` and a configured MQTT PIN are mutually
  exclusive**, now documented and warned about (POST response + boot log +
  README), since HA sends bare commands the PIN gate would reject.

### Reliability & operations (Release B)

- **Single runtime operation coordinator.** OTA (HTTP upload + Pull), WiFi
  scan, calibration and site-learning now share one atomic claim lifecycle
  (`RuntimeOperationCoordinator`) with typed failure reasons, an owner id and a
  per-operation wrap-safe watchdog that force-releases a stalled claim.
  Starting a second operation while one holds the runtime fails closed
  (`HTTP 409`, naming the real blocking operation). CSI/MQTT read the
  coordinator directly; the old service-local OTA flags were removed.
- **Unified operation status across HTTP, MQTT and HA.** `GET
  /api/operation/status` returns a common snapshot (current operation, owner,
  failure reason); a retained `security/<device>/system/operation` topic
  publishes transitions; HA auto-discovery adds a diagnostic **Runtime
  Operation** sensor. OTA conflicts now report the actual operation instead of
  `none`.

### Security — redaction & TLS trust (Release C)

- **Central secret redaction for logs and exports.** One policy masks
  credential-like keys and URI userinfo before anything reaches the serial
  console, the `/api/debug` ring, the RTC/LittleFS log mirror, `/api/logs`, the
  config snapshot/export or the diagnostic APIs — Telegram bodies/chat IDs, BLE
  SSID/passkey, MQTT/auth passwords and the disarm PIN no longer appear on any
  diagnostic surface. Config import treats a redacted `***` value as "keep
  existing", so an exported-then-imported config round-trips without wiping
  credentials.
- **Per-service TLS trust management, no hardcoded certificates.** Pull OTA,
  MQTTS, direct Telegram, Discord and the generic webhook each keep a
  write-only CA PEM in NVS (≤3072 B), managed over authorized
  `GET/POST /api/{ota,mqtt,telegram,discord,webhook}/trust` endpoints; the PEM
  is never returned in diagnostics, export or snapshot. All HTTPS clients fail
  **closed** on a missing/invalid CA; Telegram and MQTTS additionally require
  enough contiguous heap before a handshake (deferring instead of crashing
  under fragmentation); the webhook holds its CA only for the duration of the
  request, requires HTTPS and refuses redirects.
- **Config import is validated and verified.** Every imported key is
  type/range-checked (garbage or out-of-range values rejected, no dangerous
  defaults) and read back from NVS after writing, so a partial/failed import
  reports `500` instead of silently persisting a bad config.

### Build

- **CI builds all five shipped CSI/radar envs** (radar-only,
  IDF4-CSI 16/8 MB, IDF5-CSI 16/8 MB) instead of two, and each artifact
  carries a machine-readable firmware manifest (env, version string from the
  binary, git SHA, sha256).
- **Firmware version unified to a single `v5.4.1-poe-wifi` string** across
  `platformio.ini` (CSI and IDF5 `-D FW_VERSION`) and the `main.cpp` fallback,
  ending a three-way drift (`rc9` / `v5.4.0-rc2` / `rc3`) that made
  `/api/version` unable to identify the shipped build.

## [5.4.0-poe-wifi] - 2026-07-15

Detection-sensitivity release. The v5.3.x diagnostics surfaced that the
absolute variance floor was masking real motion on strong WiFi links; this
release replaces it with a link-relative floor and hardens the ML vote and
forensic logging against starved-link edge cases. Validated across a
multi-day soak on the lab node (repeated live MOTION/alarm cycles crossing
the adaptive threshold from real data, no quiet-site regressions).

### Changed

- **Link-relative threshold floor, replacing the absolute one.** The old
  `MIN_LEARNED_THRESHOLD = 0.005` was a fixed floor that sat *above* real
  walking peaks (0.001–0.003) on strong, CV-compressed WiFi links, masking
  motion by design — confirmed with controlled walk tests and by comparison
  with the working ESPectre deployment (threshold = P95 × 1.1 of the link's
  own baseline, no global floor). `csiModelRelativeFloor()` now floors the
  threshold at `max(1e-4, 3× the link's own learned quiet-mean variance)`,
  applied consistently in learning finalize, threshold estimate, EMA refresh
  and model validation. A noisy site still lands at or above the old 0.005;
  a clean link keeps its genuine sensitivity.
- **Adaptive P95 threshold now replaces the configured value once warmed up**
  (`csiEffectiveThreshold()`), instead of only ever raising it — it may lower
  the effective threshold, clamped by the new relative floor.
- **Hysteresis default 0.7 → 0.5**, matching the value already deployed
  across the working ESPectre fleet.
- **CSI traffic generator defaults to ICMP, not UDP:7.** Some ISP-provided
  routers throttle/filter unsolicited UDP to port 7 (echo — a classic DDoS
  reflection/amplification target) as a security heuristic, even for
  LAN-local self-generated traffic — one field node was capturing at ~1 % of
  its 100 pps target despite good RSSI. Switching that node's traffic
  generator to ICMP raised capture from ~1 pps to ~99.7 pps in under a
  minute. New/unconfigured nodes now default to ICMP.

### Fixed

- **ML motion vote no longer trusted below the packet-rate floor.** Two of
  the 17 MLP input features (DSER EMA, turbulence window) use packet-count
  time constants tuned for a healthy capture rate; on a starved link the same
  window stretches from ~1 s to 100+ s of real time, pushing the features far
  outside their trained distribution and saturating `ml_probability` near 1.0
  regardless of actual motion. `csiMlVoteTrusted()` now gates ML inference on
  the same packet-rate floor already used for the `packet_rate_low` health
  flag — below it, `ml_probability`/`ml_motion` are forced to 0/false instead
  of voting on unreliable data. Verified live: `ml_probability` 0.999 → 0,
  fusion source `ml` → `none`.
- **Alarm forensic `var_ratio` compared against the effective threshold.**
  `SecurityMonitor::triggerAlert` divided the trigger variance by the legacy
  static floor (`getThreshold()`) instead of the adaptive threshold actually
  used for the decision (`getEffectiveThreshold()`), so two genuine overnight
  alarms were logged as if they had fired on a near-zero variance ratio. The
  forensic ratio now reflects the threshold the detector really crossed.

### Added

- **Runtime NBVI toggle** — `POST /api/csi?nbvi=0|1` enables/disables NBVI
  subcarrier auto-selection without a reboot (diagnostic/A-B use; not
  persisted across restarts).
- **Planned-maintenance MQTT signal** — new retained topic
  `security/<device>/maintenance` (`"1"`/`"0"`), published around OTA
  updates and manual `/api/restart`, so HA automations can distinguish a
  planned reboot from a real offline/tamper event on the shared
  `availability` topic. See README § Planned-Maintenance MQTT Signal for
  the required HA-side timeout caveat.

## [5.3.1-poe-wifi] - 2026-07-12

First-night fixes for the v5.3.0 diagnostic event ring, found by running it on
two real nodes overnight. Both are event-logging fixes — detection, alarm and
API behavior are unchanged.

### Fixed

- **HEALTH_CHANGE events are debounced.** `packet_rate_unstable` compares the
  live capture rate against its own average, so near the boundary it flips every
  tick — one node logged ~2500 `health_change` events overnight and evicted every
  motion edge from the 256-slot ring. A health-flag set must now hold stable for
  ~10 ticks before it is logged (`CsiHealthDebounce`, pure + unit-tested);
  oscillation logs nothing, genuine transitions log exactly once.
- **Health transitions are evaluated even when the CSI window is empty.** The
  health check ran only after the turbulence buffer filled, so a starved link —
  the exact condition `packet_rate_low` exists to record — logged nothing at all
  (a weak-RSSI node spent most of the night starved with an empty ring).

## [5.3.0-poe-wifi] - 2026-07-11

CSI diagnostics release (návrh sekce 17, priority **P1**). Five read-only
forensics features layered on the v5.2.0 active/candidate/previous model
manager. They answer *"why didn't the alarm fire?"* — and *"is the sensor even
healthy?"* — from the device itself, without permanent external second-by-second
logging. Nothing here changes detection, alarm, MQTT control topics or PDU
behavior; every feature is diagnostic and additive.

### Added

- **P1.2 — Decision trace.** `GET /api/csi/decision` returns why the last motion
  verdict was reached: a single dominant `reason`
  (`variance_below/above_effective_threshold`, `smoothing_enter/exit_pending`,
  `breathing_hold`, `insufficient_samples`) plus the supporting values —
  variance, configured/adaptive/effective/hysteresis thresholds, smoothing
  votes, breathing-hold count, ML motion/probability, radar presence, active
  generation. A pure header-only classifier (`CsiDecisionTrace.h`) maps the
  intermediate booleans of the detector to the reason without re-running the
  thresholding math. The frontend / Home Assistant no longer has to guess which
  part of the algorithm decided.

- **P1.4 — Health reason flags.** `GET /api/csi/health` reports concrete reasons
  and a weighted 0–100 score instead of a bare boolean: `no_ht_ltf`,
  `packet_rate_low`, `packet_rate_unstable`, `wifi_roamed`, `model_missing`,
  `model_stale`, `learning_contaminated`, `radar_unavailable`,
  `mqtt_disconnected`, `clock_invalid`. `motion=false` no longer masquerades as a
  healthy sensor — a starved link, a missing model, or a floor-pinned threshold
  now surface explicitly. Pure classifier in `CsiHealthReasons.h`.

- **P1.3 — RAM diagnostic event ring.** `GET /api/csi/events?limit=100&after_seq=N`
  and `DELETE /api/csi/events`. A fixed-capacity ring (256 events, ~11 KB RAM,
  `CsiEventRing.h`) records only motion edges, rate-limited variance spikes and
  model disagreements — never per-second samples, never flash. Sequence numbers
  stay monotonic across clears so `after_seq` pagination is stable.

- **P1.1 — Shadow evaluation.** `GET /api/csi/shadow` and the retained MQTT topic
  `…/csi/model/shadow` publish a candidate model's verdict computed **in parallel**
  with the active model. `CsiShadowDetector` mirrors the variance/smoothing/
  hysteresis path with its own state, driven by the same per-tick variance but
  the candidate threshold — so feeding it can never mutate active detection.
  Agree/disagree counters reset per candidate generation; disagreements log to
  the event ring. Both surfaces are marked **`SHADOW - NO ALARM EFFECT`** and are
  never wired to alarm or PDU, letting a candidate be observed 24–72 h before apply.

- **P1.5 — Model export/import.** `GET /api/csi/site_model/export?slot=…`
  serializes any slot to portable, validated JSON (schema, algorithm-compat
  version, model fields, generation, anonymized BSSID fingerprint, CRC) — never
  the raw MAC or credentials. `POST /api/csi/site_model/import?slot=candidate`
  lands a model as a **candidate only**: it is assigned a fresh generation and
  re-validated/sealed, so a malformed or below-floor model fails with **422** and
  can never reach the active slot. Applying stays an explicit, separate step;
  `slot=active` and incompatible `algo_compat` are rejected.

### Notes

- 42 new native unit tests (`test_csi_decision` 8, `test_csi_health` 10,
  `test_csi_events` 10, `test_csi_shadow` 7, `test_csi_export` 7); full suite 127/127.
- Read-only diagnostics panel in the dashboard CSI tab; `smoke_test.py` gained a
  `check_csi_diagnostics` contract check for all five endpoints + import guard.
- `HEALTH_CHANGE` events are emitted from the detector tick (CSI-intrinsic health
  subset) and decoded to named reasons in `/api/csi/events`.
- No NVS schema change. No behavior change to detection, alarm, or existing MQTT
  control topics — the shadow topic is new and diagnostic-only.

## [5.2.0-poe-wifi] - 2026-07-11

CSI site-model lifecycle release. Long-term site learning gains a proper
candidate/apply/rollback workflow, plus three security/forensics hardening
features. **Behavior change:** a completed learning run no longer activates the
learned model automatically — it produces a *candidate* that an operator reviews
and applies. This protects a working sensor from being silently replaced by a run
captured under bad conditions, and makes a bad model one click to undo.

### Changed

- **CSI site learning finalizes to a candidate, not the active model.** A
  completed learning run now writes a `candidate` slot plus a quality report; the
  running detection model is left untouched until the candidate is explicitly
  applied. `POST /api/csi/site_learning` returns **202** on start and **409** if
  an unapplied candidate already exists (pass `replace_candidate=1` to overwrite).
  A running learning session must be stopped before a candidate can be applied.

### Added

- **Three-slot CSI model manager (active/candidate/previous).** `CsiModelManager`
  is a pure state machine over a checksummed model struct (`CsiSiteModel`,
  portable CRC32 + semantic validation). Apply copies the active model into
  `previous` then promotes the candidate to `active`, committing in-RAM state only
  after both NVS writes verify; rollback swaps active<->previous. A store failure
  at any step aborts with the in-RAM model unchanged — no half-applied state.
  Persistence via `NvsCsiModelStore` (Preferences blob + CRC per slot). The legacy
  single-model NVS layout is migrated once into the active slot on boot; boot
  detection behavior is unchanged and the legacy keys are kept for downgrade.
  27 native tests (finalize->candidate-only, atomic apply/rollback with store-
  failure injection, clear, legacy migration, EMA-touches-active-only).
- **CSI model REST API + dashboard panel.** `GET /api/csi/site_model`
  (active/candidate/previous + generation + `apply_required`), `POST .../apply`,
  `POST .../rollback`, `DELETE .../candidate`, and `GET /api/csi/model/quality`
  (p50/p90/p95/p99, mean/std/max, accepted vs. rejected-motion/rejected-radar,
  threshold clamp reason). `CsiModelOp` maps to 200/404/422/500. The CSI dashboard
  tab shows all three slots, the candidate-vs-active threshold delta (red > 50 %)
  and Apply/Discard/Rollback with before/after confirm dialogs.
- **Pre-arm health self-test.** At arm time `computeArmWarnings()` checks
  radar liveness, CSI packet rate, active-model presence/staleness (> 30 days),
  NTP clock validity and MQTT connectivity, and appends any warnings to the
  ARM/ARMING notification. It is a **warning, not a veto** — arming always
  proceeds; the point is to flag "you armed, but the radar is offline" instead of
  arming silently into a blind state. Pure/host-tested (11 native tests).
- **CSI-side tamper detection.** Anti-masking previously watched only the
  radar. `CsiTamperDetector` flags **NO_PACKETS** (packet rate collapses while
  Ethernet is up, past a 30 s grace) and **FROZEN** (running variance bit-
  identical for 2 min = stuck capture), evaluated ~1x/min in `checkSystemHealth`,
  firing one `TAMPER_ALERT` on the rising edge and clearing on recovery.
  Pure/host-tested (8 native tests).
- **Event confidence fingerprint.** `LogEvent` gains `fusion_src`
  (bit0=radar, bit1=CSI), `confidence`, `var_ratio` and `ml_prob` (56 -> 60 B);
  `triggerAlert` captures the live fusion state at every event and `/api/events`
  emits `fsrc`/`conf`/`vratio`/`mlp`, so a historical alarm is forensically
  explainable — which sensors fired it and how confident the fusion was.
  `EVENT_FILE_MAGIC` bumped `0xEE120001 -> 0xEE120002`; on upgrade the old
  `/events.bin` is re-initialized (old events dropped, never misread).

### Fixed

- **Silent boot NVS error log for CSI model slots.** `NvsCsiModelStore` guards
  slot reads with `isKey()`, so a not-yet-created slot no longer logs an NVS
  "not found" error on first boot.
- **`apply_required` compares model generation, not slot presence.** The dashboard
  Apply gating now reflects whether the candidate is actually newer than the
  running model rather than merely that a candidate slot is occupied.

## [5.1.1-poe-wifi] - 2026-07-10

Site-learning API fix release. The REST API for long-term site learning behaved
differently from what the documentation described — and the mismatch was dangerous:
a call that *looked* like a stop or status request could silently start a multi-hour
learning run.

### Fixed

- **`POST /api/csi/site_learning` unknown-parameter guard.** The handler never read the
  `action` parameter that the README had documented since v5.0.0; any request without
  `stop`/`clear_model` fell through to the default branch and **started** site learning
  (48 h default) — including `?action=stop` and `?action=status`. The handler now accepts
  `action=start` and `action=stop` as aliases for the real parameters (so calls written
  against the old docs keep working), and any other `action` value returns
  `400 Bad Request` instead of silently starting a learning run.
- **API documentation corrected.** README (Site Learning workflow + API Reference table)
  and `docs/FIRST_BOOT.md` now document the parameters the firmware actually implements:
  `?duration_s=...` / `?duration_h=...` to start (default 48 h), `?stop=1` to stop,
  `?clear_model=1` to discard the learned model, and progress/status via `GET /api/csi`
  (`learning_active`, `learning_progress`, `learning_samples`, `model_ready`, …).
  Site learning should only run while the space is empty.

### Added

- **`site_learning` contract check in `tools/smoke_test.py`.** Three sub-checks against a
  live device: unknown `action` → HTTP 400 with learning *not* started, `action=start` →
  200 + `learning_active=true`, `action=stop` → 200 + `learning_active=false`. Skips when
  CSI is inactive or learning is already running, always cleans up via `?stop=1`, and can
  be disabled with `--skip-learning`.

## [5.1.0-poe-wifi] - 2026-07-08

ESP-IDF 5.5 / Arduino 3.x migration release. The firmware now builds on the community
pioarduino platform (Arduino-ESP32 3.3.9 / ESP-IDF 5.5.4) **alongside** the legacy
espressif32@6.9.0 stack (Arduino 2.0.17 / IDF 4.4.7) — both build side by side, with
platform-specific code guarded by `ESP_ARDUINO_VERSION_MAJOR`. This release also ships a
batch of fusion, MQTT and radar-recovery fixes surfaced while hardening the IDF 5 stack on
a live CSI-only node.

### Added

- **ESP-IDF 5.5 / Arduino 3.x build targets.** New `esp32_poe_csi_idf5` and
  `esp32_poe_csi_idf5_8mb` environments on the pioarduino platform (release 55.03.39),
  sitting next to the existing 6.9.0 envs. `ETH.begin()` and `esp_task_wdt_init()` ported
  to the Arduino 3.x API behind version guards; the older stack is left untouched.
- **`tools/smoke_test.py`.** Stdlib-only HTTP smoke test against a live device: 8 ordered
  checks (version, healthz, health core, MQTT, radar/CSI, GUI, alarm FSM, Prometheus
  metrics) with PASS/WARN/FAIL/SKIP output. Radar check is adaptive (SKIP on CSI-only
  units); the alarm cycle always ends disarmed via `try/finally`. Exit 0 when no FAIL.

### Fixed

- **MQTT fail-fast on a half-open socket (IDF 5 watchdog fix).** On the Arduino 3.x stack
  `WiFiClient::write()` to a dead-but-lwIP-open socket entered a 10×1 s `select()` retry
  loop; with ~12 CSI topics per loop cycle this summed past the loopTask watchdog window.
  The first `publish()` failure now tears the transport down immediately via
  `_espClient.stop()`, so every later publish in the cycle fast-fails at the
  `connected()` guard. Oversized payloads are pre-detected before any network I/O.
- **Fusion stale-CSI gate.** Fusion falls back to radar-only when CSI data is starved —
  frozen variance/ML snapshots no longer suppress a live radar hit or hold phantom
  presence (fed from the main-loop starvation detector).
- **Radar un-veto on armed path.** Radar above the alarm-energy threshold now qualifies
  unconditionally; CSI-disagree suppression only shapes presence reporting. The
  `csiOnlyQualifies` check uses a `(source & 0x3) == 0x2` bitmask so CSI+ML agreement no
  longer blocks the CSI trigger path.
- **Radar recovery give-up latch.** A radar-less node stops recovery after two full
  hard-reset cycles (`_recoveryExhausted`, cleared on reboot), ending endless UART
  re-init churn; a radar that worked and then died keeps retrying.
- **IDF 5 watchdog hygiene.** Removed `esp_task_wdt_reset()` calls from the `radarTask`
  and pre-subscription contexts (silent no-ops on IDF 4.4, `task not found` error spam on
  IDF 5); remaining resets go through a `wdtResetSafe()` guard that only fires when the
  task is TWDT-subscribed.
- **FUSION DBG rate-limit.** Steady-state fusion debug lines are capped at 1×/10 s (state
  changes still log immediately). ML saturated on weak CSI had flooded ~527k lines in
  4.5 h and evicted the DebugLog history.

### Changed

- **CSI MQTT metrics change-gated.** Float metrics publish on >5 % delta paced to 10 s,
  states on flip, plus a 60 s heartbeat — measured ~720 → ~50 msg/min on the broker.
- **SSE queue cap 32 → 8.** A sleeping dashboard tab had queued ~48 kB of telemetry (the
  root cause of min-heap dips to ~35 kB on IDF 5, including cbuf/WebResponses allocation
  failures); the queue is now capped at ~12 kB.
- **Default entry delay 30000 ms → 0.** Unattended sites with remote disarm gain nothing
  from a keypad-style grace period.

## [5.0.17-poe-wifi] - 2026-07-06

Dashboard organization release: the Basic tab opens as a short overview on radar units too,
input placeholders are localized, and CSI metric labels speak human first, jargon second.

### Changed

- **Basic tab reorganization.** Expert radar fields — gate Min/Max (with cm readout), hold
  time, diagnostics, Bluetooth warning and calibration — moved into a new collapsed
  "Advanced radar configuration" section (radar-only, hidden on CSI-only units). Device
  name, movement sensitivity and LED toggle stay on top. No functional changes; all
  handlers and save buttons untouched.
- **CSI metric labels humanized.** "Overall motion score (composite)", "Signal variance
  (window)", "Motion exit threshold (multiplier)"; DSER/PLCR/turbulence abbreviations
  expanded in the ML help text. Human description first, technical term in parentheses —
  i18n values only, keys unchanged.

### Added

- **Placeholder localization.** New `data-i18n-ph` mechanism in `applyLang()` + 12 keys
  (cs/en). Word placeholders (Server IP, Bot Token, usernames…) now translate with the
  UI language; technical placeholders (IP examples, ports) intentionally left as-is.

## [5.0.16-poe-wifi] - 2026-06-29

Dashboard polish release: the web UI now adapts to the sensors actually present, packs
without empty gaps, hides expert detail behind collapsible sections, and finally renders
fully in the selected language on load.

### Added

- **Radar-aware dashboard.** On a CSI-only unit (no radar wired — `radar_monitoring_disabled`
  latched in `/api/health`), the UI hides all microwave-radar chrome: the distance gauge,
  movement/static readouts, radar health rows (Sensor Health / UART / Frame Rate / Comm Errors),
  the Restart Radar / Reset MW buttons, the **Gate sensitivity** and **Zones** tabs, and the
  radar-only fields on the **Basic** and **Security** tabs. The WiFi CSI status is promoted to the
  main readout, and a `Radar not connected — show` link reveals the hidden controls on demand.
  Dual-sensor units are unaffected; the UI restores radar chrome automatically after a reboot
  with the radar attached.
- **Collapsible expert sections.** CSI configuration, traffic generator, WiFi AP, actions,
  site learning, learned model, ML and the new *CSI metrics (expert)* group are collapsed by
  default, so the CSI tab opens as a short overview instead of a long scroll.

### Changed

- **Masonry card layout.** The dashboard grid now packs cards vertically (CSS columns) instead
  of stretching every card to the tallest column — eliminates the large empty areas under the
  status and health cards.
- Gate Min/Max range now shows the approximate distance in cm next to the input; Hold Time is
  edited in seconds instead of milliseconds; the **Gates** tab is renamed **Gate sensitivity**.
- CSI detection source `ml` is shown as *Machine learning*; composite/variance metrics carry
  explanatory tooltips.

### Fixed

- **Language not applied on load.** `window.onload = init` overrode the `<body onload>` attribute,
  so `applyLang()` never ran at startup and static labels stayed on their HTML defaults regardless
  of the selected language. Both now run on load, so the dashboard renders fully in the chosen
  language (English by default).
- **Telemetry flicker.** A partial telemetry frame (`{"error":"mutex_timeout"}`) no longer makes
  the radar headline blink to `idle` / `Radar disconnected`; the readout keeps its last good state.
- Re-expanding a collapsed section no longer breaks slider layout (collapse now toggles a class
  instead of clearing inline `display`).

## [5.0.15-poe-wifi] - 2026-06-29

Sensor visibility release: the main dashboard now shows CSI detection at a glance, both
sensors report clear offline/no-data states instead of stale zeros, and weak-signal / radar
loss conditions are logged and exposed for Home Assistant alerting.

### Added

- **Main-dashboard CSI indicator.** The status card is split into an **MW radar** section and a
  new **WiFi CSI** section, so a client sees CSI liveness without opening the CSI tab. States:
  `CSI offline` (not associated), `No data` (associated but no CSI frames — weak signal/AP issue),
  `idle`, `motion`. Especially useful on radar-less CSI-only units, where the panel previously
  looked dead.
- **CSI data-starvation detection.** When WiFi is associated but no CSI frames arrive for >15 s,
  the firmware logs `CSI data lost` (and `CSI data restored` once data flows again for ≥10 s, with
  hysteresis so a marginal link does not flood the log). Exposed as `csi_data_ok` in `/api/health`
  and the `poe2412_csi_data_ok` Prometheus metric.
- **Radar CSI-only latch.** On a unit with no radar wired, the radar status is logged once and then
  monitoring latches off (CSI-only until reboot) instead of repeatedly logging
  `Radar sensor connection lost`. Exposed as `radar_monitoring_disabled` in `/api/health` and the
  `poe2412_radar_monitoring_disabled` metric.
- **Inline help** for the *Prepare espota window* button explaining what it does.

### Changed

- Radar readout shows `Radar disconnected` and `—` instead of stale zeros / `NaN` when the MW
  sensor is offline.
- GUI localization completed: full Czech/English parity, no remaining hardcoded UI strings.

### Fixed

- Main panel no longer renders `NaN` when radar telemetry is momentarily unavailable.
- CSI main-panel indicator no longer flickers on a weak link (debounced).

## [5.0.9-poe-wifi] - 2026-06-26

OTA dual-homing fix. The long-standing "espota randomly fails / works on retry" problem is
finally root-caused and fixed.

### Fixed

- **espota OTA intermittently failed when the CSI WiFi shared the Ethernet subnet (dual-homing).**
  On a flat home LAN the CSI WiFi sniffer gets a DHCP lease on the *same* IP subnet as the wired
  Ethernet, so the device is dual-homed: two interfaces, one subnet. espota's UDP auth reply and
  TCP connect-back can then leave via the wrong interface and get dropped, stalling the handshake
  or upload (`No Answer to our Authentication` / `Authentication Failed` / `Error Uploading`),
  intermittently (≈50–75 %). This was repeatedly misdiagnosed as a bad flash chip, heap
  fragmentation, runtime-load starvation, or the (separate, already-fixed) LAN8720A/Digest issue —
  none of which it was. Isolated with a controlled subnet swap on a single unit (different subnet
  or CSI off: **8/8 OK**; same subnet: **6/8 fail**; same flash chip throughout).

  Fix: the device now **single-homes itself for the flash**. `CSIService::wifiDownForOta()` stops
  the traffic generator, disables WiFi auto-reconnect, and drops the CSI WiFi STA; the reconnect
  loop is already gated on the OTA-in-progress flag, so WiFi stays down for the whole window.
  It is called from `ArduinoOTA.onStart` and — crucially, *before* the espota auth exchange — from
  the `/api/ota/espota/prepare` maintenance window, and reversed centrally in
  `otaRuntimeRestoreServices()` (covers error/watchdog/window-timeout; a successful flash reboots
  and WiFi returns on boot). Since the standard espota deploy flow already calls `prepare`, this is
  automatic — no VLAN, no manual CSI toggle. Validated on hardware: a dual-homed unit that failed
  6/8 before now flashes **8/8**. See [docs/OTA_OPERATIONS.md](docs/OTA_OPERATIONS.md) → *Dual-Homing*.

## [5.0.8-poe-wifi] - 2026-06-24

OTA reliability + config-import fixes. Validated end-to-end on a remote, Ethernet-only node.

### Fixed

- **OTA / config-write "empty reply" (root cause)** — in the pinned ESPAsyncWebServer fork, authenticated body-POST handlers ran the auth check *after* the whole request body was streamed. Digest's `401`→retry then lost the buffered body (and its nonce is fragile under LAN8720A write-buffer backpressure), producing empty replies / 180 s upload hangs. `/api/update`, `/api/update/pull` and `/api/config/import` now authenticate with stateless **Basic** (sent preemptively, so the body survives), and the upload callback closes the connection on auth failure instead of letting a multi-MB image drain first. Pull OTA verified end-to-end over the network.
- **`/api/config/import` silently dropped every string field** — ArduinoJson 7.4.3 `JsonVariant::is<String>()` returns false for JSON strings, so all string keys (`mqtt_*`, `hostname`, `auth_*`, `csi_ssid`/`csi_pass`, `tg_*`, `static_*`, `sched_*`, `zones`) were skipped while numeric fields imported fine. All 19 guards switched to the canonical `is<const char*>()`.
- **No-response auth paths** — `checkAuth`/`checkAuthBasic` now send `503` instead of a bare `return` when config is unavailable, so a failure can no longer masquerade as a dropped connection.

### Added

- **Out-of-coverage WiFi diagnostics over API** — `/api/csi` always exposes `wifi_status`, `wifi_last_reason`, `wifi_reconnects`, `wifi_rssi`, `wifi_ssid`/`bssid`/`channel`/`ip` (no longer gated on CSI being active). A node that drifts out of WiFi range stays fully observable over Ethernet — no serial needed. Each background reconnect attempt is also logged to serial. Adds the STA disconnect reason code for remote diagnosis.
- **8 MB flash board variant** — `[env:esp32_poe_csi_8mb]` + `partitions_8mb.csv` for ESP32 boards shipped with 8 MB flash (the 16 MB layout bootloops on them: `spi_flash: Detected size(8192k) smaller than header(16384k)`).

## [5.0.7-poe-wifi] - 2026-06-11

Quick-wins release — IMPROVEMENTS T1/T2/T3/T4. First release with automated unit tests.

### Added

- **`/metrics` endpoint (T1)** — Prometheus text exposition (heap, chip temp, radar health, ETH/MQTT state, alarm state, fusion confidence, CSI packet stats). Basic auth; scrape with `basic_auth` in `prometheus.yml`.
- **Per-IP auth lockout (T3)** — 5 failed login attempts (with credentials) within 60 s lock the source IP out with `429 Retry-After`, exponential backoff 30 s → 15 min. Successful login clears the record. Pure-logic core in `include/services/AuthLockout.h`.
- **Native unit tests (seed of T5)** — new `[env:native]` + Unity; `pio test -e native` covers AuthLockout and the metrics builder. Runs in CI.
- **cppcheck in CI (T2)** — `tools/run_cppcheck.sh` (same invocation locally and in CI), `--error-exitcode=2`. Existing findings triaged: printf format casts fixed, copy ctors deleted on buffer-owning services, documented inline suppressions elsewhere.

### Fixed

- **Dashboard `<html lang>` (T4)** — was hardcoded `cs` although the default UI language is EN since v5.0.4; now defaults to `en` and follows the i18n language switch.

## [5.0.6-poe-wifi] - 2026-06-11

OTA field-service hardening release. Motivated by a long-standing problem: OTA that works right after a flash but fails on units with long uptime (heap fragmentation, AsyncTCP backpressure under radar+CSI+MQTT load, and `ArduinoOTA` CPU starvation). See [docs/OTA_OPERATIONS.md](docs/OTA_OPERATIONS.md) → "Why OTA Gets Harder the Longer a Device Has Been Running".

### Added

- **Guarded Pull OTA deploy helper** — `tools/pull_ota_deploy.sh` performs dry-run target identity checks, computes MD5, starts a temporary LAN server, and only flashes when `--flash` is explicitly passed. `--cold-reboot` reboots a long-uptime unit and waits for it to return before pulling (defragments heap / clears AsyncTCP).
- **OTA operations guide** — `docs/OTA_OPERATIONS.md` documents safe OTA prerequisites, Pull OTA flow, espota maintenance use, rollback limits, and AI-agent rules.
- **ESPOTA maintenance diagnostics** — `/api/ota/status` reports OTA owner/progress/timeout state and `/api/ota/espota/prepare` opens a bounded maintenance window.

### Changed

- **Pull OTA MD5 is mandatory** — backend now rejects Pull OTA requests without a valid 32-character MD5. The web UI marks MD5 as required.
- **Pull OTA no longer follows HTTP redirects** — the `isPrivateLanUrl()` whitelist only gated the initial URL, so a `30x` to an off-LAN host could defeat it (SSRF) and leak the forwarded `Authorization` header to the redirect target. Redirects now fail with a clear message; point the URL directly at the firmware `.bin`.
- **Pull OTA success reboots through `safeRestart("ota_complete")`** — reset history and heap diagnostics are preserved instead of calling `ESP.restart()` directly.
- **README OTA guidance** — network update docs now prefer guarded Pull OTA with MD5 and mark multipart upload as fallback only.

### Fixed

- **Pull OTA MD5 integrity was never enforced** — `Update.setMD5()` was called *before* `Update.begin()`, which resets the expected hash to empty, so the firmware digest was never actually checked and any binary with a valid-hex MD5 would flash. `setMD5()` now runs after `begin()`; a deliberately wrong MD5 is now correctly rejected (`UPDATE_ERROR_MD5`). Verified live on bench.
- **OTA runtime overlap and stale cleanup risk** — multipart, Pull OTA, and espota now share an OTA runtime owner/progress state. A main-loop watchdog aborts stale update state, clears CSI/MQTT OTA flags, resumes radar, and records timeout status if an OTA path stops making progress.

## [5.0.4-poe-wifi] - 2026-06-05

Security hardening release — MQTT alarm PIN guard, dashboard ARM block on default credentials, and HTTP security headers.

### Added

- **MQTT alarm PIN guard** — `security/{id}/alarm/set` now requires `CMD:pin` format when `sec_mqtt_pin` is configured in NVS. Commands without the correct PIN are rejected. New API endpoint `POST /api/security/mqtt-pin?pin=<pin>` sets or clears the PIN.
- **HTTP security headers** — all web responses now include `X-Frame-Options: DENY`, `X-Content-Type-Options: nosniff`, and `Referrer-Policy: no-referrer`.

### Changed

- **ARM blocked on default credentials** — the web dashboard blocks ARM/DISARM actions when the admin password is still the factory default (`admin`/`admin`). The UI shows a warning prompting the operator to change the password first.
- **Default dashboard language** — changed from Czech to English so new deployments open in English by default; language preference is still persisted in localStorage as before.
- **OTA password moved to `platformio.ini.local`** — the upload password is no longer stored in the committed `platformio.ini`. Copy `platformio.ini.local.example` to `platformio.ini.local` and set your password there.

## [5.0.3-poe-wifi] - 2026-05-19

### Fixed

- **i18n: `gate_legend` not translated to Czech** — CS entry was a copy of the EN string. Translated to Czech.
- **Gate tab corrupted legend HTML** — Mixed Czech/English text, broken `&#9632;` entity references, and extra `</span>` tags (merge artifact). Replaced with clean `data-i18n` wiring so both language mutations render correctly.

## [5.0.2-poe-wifi] - 2026-05-02

OTA reliability, auth stability, ARM_HOME mode, and crash forensics.

### Added

- **ARM_HOME mode** — new alarm state for stay-at-home scenarios. MQTT command `ARM_HOME` activates the arm sequence with a separate perimeter profile; state persists across reboots via NVS.
- **RTC uptime tracker** — `RTC_DATA_ATTR` counter updated every main-loop tick. Survives Task WDT / panic / SW reset (cleared only on power-on / brownout), giving ~1 s resolution on crash time in `reset_history` instead of the previous 1 h NVS granularity.
- **Pre-trigger ring buffer** — `/api/alarm/status` now includes the last N motion events before the alarm fired, giving operators forensic context for investigating false positives.
- **`/healthz` liveness endpoint** — unauthenticated, returns heap + uptime so external monitors can distinguish a live-but-auth-degraded device from a dead one.
- **`xTaskCreatePinnedToCore` failure handler** — startup task creation failures are now logged and surfaced instead of silently ignored.

### Fixed

- **MQTT heap fragmentation → OTA stall** — the 200-slot offline publish buffer churned heap allocations during an active OTA upload, fragmenting free memory enough to stall `ArduinoOTA` authentication. Fix: drop-on-floor mode when `CSIService::isOtaInProgress()` is set; a separate `g_otaRebootForce` flag ensures the slot swap still completes even when the reboot-inhibit is on.
- **Pull-OTA endpoint shadow** — `AsyncWebServer`'s backward-compatible prefix matcher routed `POST /api/update/pull` to the multipart-upload handler (which stalls at 65 536 B) instead of the pull handler. Fix: `AsyncURIMatcher::exact()` on the pull route.
- **Write-buffer crash on heavy JSON endpoints** — `GET /api/zones`, `GET /api/security/config`, and `GET /api/alarm/status` (with ring buffer) triggered `RemoteDisconnected` under sustained polling because Digest auth maintains per-request nonce state that AsyncTCP on LAN8720A drops under backpressure. Fix: switched those endpoints to Basic auth (stateless challenge), the same approach already used for `/api/update`.

### Changed

- **AsyncTCP / ESPAsyncWebServer pinned** — `lib_deps` now references specific commits known to work on ESP32 + LAN8720A to prevent silent regressions from upstream changes.

## [5.0.1-poe-wifi] - 2026-04-26

Bug fix release for v5.0.0 — pull-OTA hardening, alarm and runtime stability.

### Security

- **Pull-OTA URL whitelist** — `POST /api/update/pull` now rejects URLs that don't resolve to a private RFC1918 host (`192.168/16`, `10/8`, `172.16/12`) or an mDNS `*.local` / `*.lan` name. Limits the blast radius of a stolen admin password to the local LAN.
- **Pull-OTA MD5 verification** — body accepts an optional `md5` field (32 hex chars). When present, the device passes it to `Update::setMD5()` before streaming the image with `Update::writeStream()`, and `Update::end(true)` fail-closes on mismatch. The previous flow had no integrity check on the downloaded binary.

### Fixed

- **Pull-OTA plain-HTTP timeout** — `WiFiClient::setTimeout()` was called with `30` (interpreted as 30 ms by ESP32 arduino-core), so HTTP pulls timed out almost immediately. Now `30000` ms, matching `WiFiClientSecure`.
- **`/api/config/import` and `/api/zones`** — oversize bodies (> 4 KB) and malformed JSON used to silently 200 and reboot. They now return `413 Payload Too Large` or `400 Invalid JSON` with a descriptive message.
- **HTTP OTA failure visibility** — failed multipart uploads now publish to system log and Telegram alert instead of only printing to the serial console.
- **LD2412 `update()` mutex timeout** — raised from `2 ms` to `50 ms`. The previous value silently dropped radar frames under CSI / MQTT load.
- **LD2412 frame value clamping** — distance and energy fields are clamped to datasheet ranges (`0..600 cm`, `0..100`) before propagating to the alarm logic, so a corrupted UART byte can't fake a zone hit.
- **LD2412 hard-reset baud verification** — after a recovery `_serial->begin()` the service now calls `readFirmwareVersion()` and falls back to the alternate baud (115200 ↔ 256000) once if the radar doesn't respond.
- **`SecurityMonitor::update()` mutex timeout** — raised from `200 ms` to `500 ms`; matches the `setArmed()` budget and prevents critical state transitions from being deferred under fusion load.
- **Sticky reflector static-filter** — cleared on `DISARMED → ARMING` so the exit delay starts with a clean filter and a quiet pre-arm window can't latch the filter into the armed state.
- **Scheduled arm / disarm** — validates `HH:MM` ranges, latches per `tm_yday`, and fires once per day per direction. Late ticks (`HH:MM:30` instead of `:00`) are no longer missed.
- **`EventLog::flushToDisk()` heap-fail spin** — bumps `_lastFlush` when `LogEvent[]` allocation fails so the next flush attempt is deferred by the rate-limit window. Fixes a hot-loop that held the mutex under heap pressure.
- **`isPrivateLanUrl()` 172.16/12 parser** — used `indexOf('.', 3)`, which returned the literal `.` at index 3 and left the second-octet substring empty, so 172.x.x.x addresses were wrongly rejected by the whitelist.

### Changed

- **MQTT offline buffer** — capacity raised from 50 to 200 slots (~57.6 KB on LittleFS) so a 5-minute outage no longer drops state-change history from the Home Assistant timeline.
- **CSI WiFi hostname** — aligned with the Ethernet hostname so mDNS advertises a single identity for the device.
- **DMS counter NVS reset** — read-before-write on the success path avoids a redundant flash erase when the counter is already zero.

## [5.0.0-poe-wifi] - 2026-04-25

Major release consolidating four months of CSI work, radar fusion improvements, and operational hardening.

### Added — CSI presence detection

- **WiFi CSI motion detection** — sniffer mode pulls Channel State Information from frames between the configured AP and ESP, exposes per-packet variance / turbulence / phase-turbulence / breathing / DSER / PLCR features, and contributes presence + confidence into the fusion engine. Configured via `/api/csi/wifi` (SSID/pass) or `secrets.h` build-time defaults.
- **Site learning** — quiet-room baseline learner samples 30 min – 168 h, persists `mean / std / max variance` and a derived threshold to NVS, and refreshes the model continuously via EMA so it adapts to slow environmental drift. Full GUI controls (start/stop/progress/elapsed) on tab 6.
- **MLP motion classifier** — 17-feature shallow neural network (DSER, PLCR, variance, phase-turbulence, breathing, …) compiled into the firmware (`include/services/ml_features.h` + `ml_weights.h`). Acts as third detection signal in the fusion path; F1 = 0.852 on the validation dataset.
- **3-way fusion** — radar + CSI variance + ML probability combined into a single `fusion_source` field surfaced over MQTT and `/api/csi`. ML serves as tiebreaker when the two physical signals disagree; cannot trigger an alarm by itself in `refl_auto` zones (kinetic floor still required).
- **NBVI subcarrier auto-selection** — every 500 packets the service ranks the 12 subcarriers by coefficient of variation and keeps the K=8 most stable. Drops noisy SCs without user tuning. Exposes `nbvi_*` diagnostics.
- **Adaptive P95 threshold** — 300-sample rolling buffer raises the variance threshold by 1.1× when ambient noise spikes (only raises, never lowers) to suppress AP-induced false positives.
- **Stuck-motion auto-raise** — if continuous motion lasts > 24 h, threshold is multiplied by 1.5× (max 3 raises per boot) and reset on next quiet window.
- **BSSID-change baseline reset** — AP roam invalidates the learned model so the next quiet period rebuilds it cleanly.
- **AP compatibility probe** — `ht_ltf_seen` flag in `/api/csi` lets HA operators verify the AP actually emits the HT_LTF frames CSI needs. Documented known-working / known-failing AP list in README.

### Added — operational

- **Pull-based OTA endpoint** — `POST /api/update/pull` accepts a JSON body with a firmware URL, fetches the image with `HTTPClient` + `httpUpdate`, and persists phase/error to NVS. `/api/update/pull/status` exposes progress. Robust against HTTP/HTTPS, follows redirects, freezes MQTT + radar tasks during the swap.
- **Cold-reboot-before-OTA GUI flow** — default-on checkbox in the upload form posts `/api/restart`, polls `/api/health` until uptime < 30 s, then uploads. Eliminates the second-OTA-in-boot stall on this hardware (3/3 success in bench, was 1/3 without).
- **Schedule arm/disarm** — daily auto-arm and auto-disarm times configurable in the GUI; `auto_arm_minutes` delay supports staged morning routines.
- **Network tab** — static-IP / DHCP / DNS configuration with validation.
- **Timezone picker** — IANA TZ string + DST offset stored in NVS.
- **Config export / import** — full `/api/config/export` snapshot includes radar, alarm, MQTT, CSI, Telegram, schedule, network, TZ. JSON file roundtrips through `/api/config/import`.
- **Config snapshot before OTA** — backup written to NVS so the operator can restore on rollback.
- **Heartbeat MQTT topic** — dedicated `<prefix>/heartbeat` with uptime payload defeats Home Assistant deduplication of identical messages.
- **Site-learning persistence** — learned site model survives OTA via NVS.
- **Telegram test endpoint** — `POST /api/telegram/test` sends a probe message so the operator can verify token/chat_id without arming.

### Added — repo / project

- `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `.github/ISSUE_TEMPLATE/*`, `.github/pull_request_template.md`, `.github/workflows/build.yml` (CI build).
- `docs/MAINTAINING.md`, `docs/RELEASE_CHECKLIST.md`.
- `tools/ha_csi_push.py` — REST-state push helper for sites that prefer pull-from-HA over MQTT.
- README section: "WiFi Access Point Requirements for CSI" with known-working / known-failing AP list.

### Changed

- All JSON `GET` and `POST`-response handlers in `src/WebRoutes.cpp` now stream directly to TCP via `AsyncResponseStream` (was: build into `String`, then `request->send`). Eliminates the transient heap-spike that produced sustained `RemoteDisconnected` / empty-reply failures under continuous polling on weak-RSSI deployments.
- `LD2412_Extended` library now exposes `setBluetooth()`, `readMacAddress()`, and the renamed DBC API.
- Radar Bluetooth disabled by default on boot (security: prevents HLK app pairing without explicit operator action).
- Reconnect-replay invalidation extended to TIER 3 telemetry (uart_state, frame_rate, heap, …) so deadband-gated values re-fire on next publish after MQTT reconnect.
- 5-min heartbeat republish of TIER 3 diagnostics so HA `last_reported` cannot freeze on healthy-but-stable values.

### Fixed

- **MQTT recovery** — fail-streak counter, soft-recovery on N consecutive publish failures, cache invalidation on reconnect.
- **DMS restart loop** — overflow guard in `(now - lastPublish)` after ~49 days uptime; `_lastPublish` reset on MQTT connect.
- **HTTP OTA stall** — first-OTA-after-cold-boot succeeds reliably; subsequent OTAs in the same boot session require the GUI cold-reboot step (root cause is in the AsyncTCP / heap layer, not the Update library — workaround documented).
- **Static-zone false alarms** — sticky static-filter (5-frame clear) prevents short move-energy spikes in `refl_auto` zones from promoting to PENDING; `_isStaticFiltered` guard added to `radarQualifies`.
- **CSI fusion source buffer** — short-window buffer prevents `fusion_source` from flapping on the boundary frame between radar-only and ML-only branches.
- **i18n** — fixed 7 broken Czech strings in the GUI translation table.
- **Out-of-range radar gate styling** — gate visualisation now clamps to the configured display range.
- **Learning progress label** — shows elapsed / target rather than just percentage.

### Deprecated / removed

- ESPAsyncWiFiManager removed (Ethernet is the only management transport on this board).

### Bench numbers

- RAM: 20.6 % (67 372 / 327 680 B)
- Flash: 36.6 % (1 536 885 / 4 194 304 B)
- HTTP failure rate under sustained polling: < 1 % (was 94 % on production deployment with weak RSSI before the streaming fix)

### Migration notes

- Existing v4.5.x deployments can OTA directly to v5.0.0.
- Site-learning model persists across the upgrade — no need to re-learn unless you moved the sensor.
- If your AP is not in the known-working list, watch `/api/csi` for `ht_ltf_seen=true` after first boot. If it stays `false`, see the README troubleshooting row.
- New defaults are conservative; review the schedule, network, and TZ tabs after upgrade.

## [4.5.5-poe-wifi] - 2026-04-20

### Fixed
- **Engineering Mode initial state** — Home Assistant showed `Engineering Mode` as `Unknown` until it was toggled, because the change-gated MQTT publish never fired when boot state matched the zero-initialized cache. Added `lastPub.eng_mode` to the reconnect-replay invalidation block so the first post-connect pass always publishes the real state.

## [4.5.4-poe-wifi] - 2026-04-19

### Added
- **DSER (Dynamic-to-Static Energy Ratio)** — per-packet CSI feature from Uni-Fi paper (arXiv 2601.10980): `log(|H_d|²/|H_s|²)` averaged across 12 selected subcarriers, with slow EMA (α=0.01, ~100-packet time constant) tracking the static component. Negative in absence (-6..-4), rises toward 0 with motion. Published as `<prefix>/dser` and exposed via `/api/csi/status` + SSE telemetry.
- **PLCR (Path-Length Change Rate proxy)** — RMS of unwrapped inter-packet phase delta divided by 2π. Per-SC phase delta wrapped to `[-π,π]`. ~0 at rest, 0.3-0.5 during walking. Published as `<prefix>/plcr`.
- **RSSI health check** — diagnostic warnings when RSSI >-40 dBm (near-AP saturation) or <-70 dBm (low SNR). Reuses existing 30s throttle in `CSIService::update()`.

### Changed
- `resetIdleBaseline()` now clears `_csiStatic`, `_csiPhasePrev`, and the feature convergence state — takes ~200 packets (~2s) to stabilize after reset.

## [4.5.3-poe-wifi] - 2026-04-18

### Fixed
- **OTA Digest re-auth stall at 64KB** — auth now runs once on first chunk and is tracked via static `otaAuthorized` flag; previously AsyncWebServer re-validated Digest auth on each chunk, causing ETH connection drop on LAN8720A after 64KB buffer
- **OTA commit guard** — `Update.end(true)` now runs only when `Update.hasError()` is false; on error path `Update.abort()` is called to discard partial image instead of potentially committing a corrupted firmware
- **OTA auth flag reset** — `otaAuthorized` cleared on `final` chunk (success or error) so stale auth state cannot leak into subsequent uploads
- **Web body upload bounds check** — `/api/zones`, `/api/config/import`, and `/api/security/event/ack` body handlers now verify `index + len <= total` before `memcpy()` to prevent heap overflow if a malformed client sends more data than declared `total`

## [4.5.1-poe-wifi] - 2026-04-18

### Added
- **Fusion → alarm** — CSI-only presence can trigger ARMED→PENDING→TRIGGERED; radar false positives suppressed when CSI disagrees (fusion moved before alarm logic)
- **Auto-zones from learning** — `POST /api/radar/apply-learn` creates ignore_static_only zone from reflector learn results with overlap detection
- **Event timeline UI** — 24h density heatmap, type filtering, pagination, CSV export button
- **CZ/EN language toggle** — i18n dictionary with `t()` helper and `data-i18n` attributes; language persisted in localStorage; eliminates need for separate repo copies
- **Traffic generator tuning** — configurable target port (`traffic_port`), ICMP ping mode (`traffic_icmp`), PPS rate (`traffic_pps`) via `/api/csi` POST; GUI controls in CSI tab
- **Multi-sensor mesh verification** — MQTT-based peer alarm cross-validation with 5s confirm window
- **Supervision heartbeat** — 60s peer alive publish, 3min offline alert with tamper notification
- **GUI screenshots** — docs/screenshots/ with anonymized dashboard captures

### Fixed
- **DMS millis() overflow** — after ~49.7 days uptime, `(now - _lastPublish)` wraps to UINT32_MAX causing infinite MQTT reconnect loop; added overflow guard (ignore age > 30 days) and reset `_lastPublish` on MQTT connect
- **OTA delay** — 500ms delay before reboot so HTTP response passes through nginx proxy (fixes 502)
- **Event API parsing** — frontend read events as flat array but API returns `{events:[...], total, ...}` object
- **CSV export** — `doc.as<JsonArray>()` → `doc["events"].as<JsonArray>()`

### Changed
- Alarm notifications now show fusion source (radar/csi/both)
- CSI-only alarm uses entry delay (behavior=0) with zone="csi_only"

## [4.2.0-poe-wifi] - 2026-04-13

### Added
- **WiFi CSI: ESPectre port** — Hampel outlier filter (MAD-based, window=7)
- **WiFi CSI: Low-pass filter** — 1st-order Butterworth IIR at 11 Hz cutoff
- **WiFi CSI: CV normalization** — gain-invariant turbulence (std/mean) for ESP32 without AGC lock
- **WiFi CSI: DNS traffic generator** — FreeRTOS task sending UDP queries to gateway at 100 pps
- **WiFi CSI: Breathing-aware presence hold** — prevents dropping stationary person (~5 min max)
- **WiFi CSI: HT20/11n WiFi forcing** — consistent 64 subcarriers with guard-band-aware selection
- **WiFi CSI: STBC packet handling** — collapsed doubled packets (256→128 bytes)
- **WiFi CSI: Short HT20 handling** — 114-byte packets remapped with left guard padding
- **WiFi CSI: CSI packet length validation** — rejects non-standard packets
- **Radar: Entry/exit path validation** — zone `valid_prev_zone` field, invalid path → immediate trigger
- **API: Event timeline** — `current_zone`, `debounce_frames`, `last_event` in `/api/alarm/status`
- **API: Debounce frames** — configurable via `/api/alarm/config` POST

### Changed
- Radar processing tick: 1s → **50ms** (20 Hz) to catch short detections
- MQTT TIER 1 state changes: lastPub cache updated only on successful publish
- CSI subcarriers: `{6,10,...}` → `{12,14,16,18,20,24,28,36,40,44,48,52}` (out of guard bands)
- CSI temporal smoothing: 3/6 enter → **4/6** (matches ESPectre MVS)
- CSI idle amplitude baseline: placeholder → real amplitude sum
- CSI two-pass variance for per-packet turbulence (numerically stable on float32)

## [4.1.4-poe-wifi] - 2026-04-12

### Added
- WiFi CSI runtime configuration via REST API and GUI
- CSI tab in web dashboard with live sparkline graph
- Auto-calibration, idle baseline reset, WiFi reconnect actions
- CSI metrics in SSE telemetry stream

## [4.1.3-poe-wifi] - 2026-04-10

### Changed
- Swapped me-no-dev/AsyncTCP + ESPAsyncWebServer for ESP32Async community fork
- Fixes race conditions in digest auth parser and TCP close handling

## [4.1.2-poe-wifi] - 2026-04-09

### Fixed
- SSE live telemetry buffer regression from LD2412 v3.10.0 port

## [4.1.1-poe-wifi] - 2026-04-08

### Changed
- Security hardening from ESPHome 2026.3 community audit

## [4.1.0-poe-wifi] - 2026-04-05

### Added
- Static IP configuration
- Scheduled arm/disarm with timezone support
- CSV event export
- Auto-arm after configurable idle period
- Heap optimizations

## [4.0.6-poe-wifi] - 2026-03-28

### Added
- Heap diagnostics with crash guards and bounds validation

## [4.0.5-poe-wifi] - 2026-03-27

### Added
- Telegram alerts for low RAM (warn/critical/recover thresholds)

## [4.0.4-poe-wifi] - 2026-03-26

### Added
- Telegram alerts for chip temperature

## [4.0.3-poe-wifi] - 2026-03-25

### Added
- Chip temperature MQTT publishing with configurable interval

## [4.0.2-poe-wifi] - 2026-03-24

### Added
- OTA rollback bootloader
- Chip temperature monitoring

## [4.0.0-poe-wifi] - 2026-03-22

### Added
- LAN8720A PHY LED control via MDIO

## [3.9.9-poe-wifi] - 2026-03-21

### Added
- Web assets on LittleFS with PROGMEM fallback

## [3.9.8-poe-wifi] - 2026-03-20

### Added
- MQTT offline buffer: queue messages to LittleFS when disconnected

## [3.9.5-poe-wifi] - 2026-03-18

### Added
- Initial WiFi CSI implementation (basic turbulence, phase, ratio, breathing)
- WiFi STA mode alongside Ethernet for CSI-only capture
