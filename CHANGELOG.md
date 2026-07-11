# Changelog

All notable changes to this project will be documented in this file.

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
