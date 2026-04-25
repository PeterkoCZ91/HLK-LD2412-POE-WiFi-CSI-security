<!--
Thanks for contributing! A few ground rules:
- Keep the PR focused on one change. Separate refactors from behavior changes.
- Verify on real hardware when the change touches radar, CSI, OTA, or MQTT.
- The public repo tracks stable releases; rapid dev iteration happens on the private branch.
-->

## Summary

<!-- 1–3 sentences describing what this PR changes and why. -->

## Type of change

- [ ] Bug fix (non-breaking)
- [ ] New feature (non-breaking)
- [ ] Breaking change (config, MQTT topic, API shape, or behavior)
- [ ] Documentation / tooling only
- [ ] Build / CI

## Affected areas

- [ ] Radar (LD2412) / zones / alarm state machine
- [ ] WiFi CSI / fusion / site learning / MLP
- [ ] Web dashboard / API
- [ ] Home Assistant / MQTT
- [ ] Telegram bot
- [ ] OTA / update flow
- [ ] Build system / CI

## Hardware tested on

<!-- Board, firmware version, AP, radar module. -->

- Board:
- Firmware version:
- AP (if CSI-related):

## Test plan

<!-- How did you verify this works? Manual steps, API snapshots, etc. -->

- [ ]
- [ ]

## Checklist

- [ ] I ran `pio run -e esp32_poe_csi` (or the relevant environment) and the build succeeds.
- [ ] I verified behavior on real hardware (or marked the PR as doc-only / CI-only).
- [ ] I updated `CHANGELOG.md` if this is user-visible.
- [ ] I updated `README.md` if I introduced new API endpoints, MQTT topics, or build flags.
- [ ] I did **not** commit secrets (`include/secrets.h`, credentials, tokens).
