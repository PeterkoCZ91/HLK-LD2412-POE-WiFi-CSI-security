# Security Policy

## Supported versions

Security fixes are applied to the latest released version on `main`.

| Version  | Supported          |
| -------- | ------------------ |
| `v5.x`   | :white_check_mark: |
| `v4.5.x` | :x: (upgrade to `v5.x`) |
| `< v4.5` | :x:                |

## Reporting a vulnerability

**Do not open a public GitHub Issue for security problems.**

Please report privately via one of:

1. **GitHub Security Advisories** — preferred.
   Use the [Report a vulnerability](https://github.com/PeterkoCZ91/HLK-LD2412-POE-WiFi-CSI-security/security/advisories/new) button
   on the repository's Security tab. This gives us a private channel and lets
   us collaborate on a fix before disclosure.

2. **Email** — `pisakpetr@gmail.com`. Please include "SECURITY" in the subject.

Include in the report:

- A description of the issue and the potential impact.
- Steps to reproduce or a proof-of-concept.
- Affected firmware version (from `/api/version`) and build environment.
- Whether the issue is exploitable remotely, from the local LAN only, or only
  with device-side access.

## What to expect

- Acknowledgement within **7 days**.
- Initial assessment and severity classification within **14 days**.
- Coordinated disclosure once a fix is available. We credit reporters who
  want credit; we keep reporters anonymous on request.

## Scope

In scope:
- Authentication, authorization, or session handling on the web UI or API.
- OTA update flow (image verification, auth, rollback).
- MQTT / Home Assistant integration (message injection, topic hijacking).
- Default credentials, hardcoded secrets in shipped firmware.
- Remote code execution, privilege escalation, or persistence on the ESP32.

Out of scope:
- Denial-of-service against a single device on a network the attacker already
  has access to (this device is not designed for hostile LAN environments).
- Physical tampering with the ESP32 board or flash.
- Issues requiring Home Assistant, MQTT broker, or router misconfiguration
  unrelated to this firmware.
