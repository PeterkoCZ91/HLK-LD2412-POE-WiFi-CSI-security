# Contributing

Thanks for your interest in contributing! This project values:

- **Hardware-verified changes** over synthetic tests.
- **Small, focused PRs** over large mixed-purpose ones.
- **Clear reproduction steps** in bug reports.

## Project layout

```
src/          Application code (main.cpp, services, state machines)
include/      Public headers, configuration (secrets.h.example)
lib/          Vendored libraries
docs/         Screenshots and documentation assets
tools/        Host-side Python helpers
platformio.ini Build environments (esp32_poe, esp32_poe_csi, ota_*)
```

## Development setup

### Prerequisites

- **PlatformIO Core** (via pip: `pip install platformio`) or the VS Code extension.
- An ESP32 board with Ethernet + PoE (the primary target is the Prokyber
  ESP32-Stick-PoE-A), an **HLK-LD2412** radar module, and — for CSI builds — a
  2.4 GHz access point with a peer client generating traffic.
- Python 3.10+ for the host-side tools under `tools/` and `observe_161.py`.

### First build

```bash
git clone https://github.com/PeterkoCZ91/HLK-LD2412-POE-WiFi-CSI-security.git
cd HLK-LD2412-POE-WiFi-CSI-security
cp include/secrets.h.example include/secrets.h
cp include/known_devices.h.example include/known_devices.h
# edit include/secrets.h with your WiFi / MQTT / OTA credentials
pio run -e esp32_poe_csi
pio run -e esp32_poe_csi --target upload --upload-port /dev/ttyUSB0
```

For OTA updates to an already-flashed device, see the OTA section in the
[README](README.md#ota-updates).

## Branching model

- `main` — stable releases only. Direct commits are allowed for
  documentation or trivial fixes; larger changes go through a PR.
- Feature branches — use descriptive names: `feat/csi-adaptive-threshold`,
  `fix/mqtt-reconnect-deadlock`, `docs/readme-ap-compat`.
- Rapid experimental iteration lives on a private development fork; only
  verified, squashed changes land on public `main`.

## Commit messages

Conventional-commits style, but relaxed:

```
type: short summary (≤ 72 chars)

Optional body explaining the why, not the what.
```

Common types: `feat`, `fix`, `docs`, `refactor`, `chore`, `build`, `tools`.

## Pull requests

Before opening a PR:

1. `pio run -e esp32_poe_csi` builds cleanly.
2. If the change affects detection, OTA, MQTT, or the alarm state machine,
   verify on real hardware and include a short test-plan in the PR.
3. Update `CHANGELOG.md` for user-visible changes.
4. Do not commit `include/secrets.h`, tokens, or real MAC addresses.

The PR template will ask about affected areas and hardware tested on —
please fill it in; it speeds up review.

## Code style

- C++17, matches existing file style (no strict formatter enforced).
- Avoid dynamic allocation in the radar / CSI hot paths.
- New API endpoints go in `src/WebRoutes.cpp`; follow the existing auth
  wrapper pattern.
- New MQTT topics must be documented in `README.md` under Home Assistant
  integration and listed in `CHANGELOG.md`.

## Reporting bugs and asking questions

- **Bugs:** use the [bug report template](.github/ISSUE_TEMPLATE/bug_report.yml).
  Include the three diagnostic snapshots (`/api/health`, `/api/csi`,
  `/api/telemetry`) — that's almost always the first thing we'd ask for.
- **Questions / setup help:** use Discussions (once enabled) rather than Issues.
- **Security issues:** see [SECURITY.md](SECURITY.md). Do not open a public Issue.

## License

By contributing, you agree that your contributions are licensed under the
GPL-3.0 License, the same license as the project.
