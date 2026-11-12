# Security

## Reporting a vulnerability

Please report security issues privately through
[GitHub private vulnerability reporting](https://github.com/petr-komin/esp32-voice-assistant/security/advisories/new),
not in public issues. You can expect a first response within a week.

## Threat model (summary)

The device is a battery-powered microphone in a private space that can change
the heating schedule of a remote building. The main assets are therefore the
**audio** (privacy), the **control path** to the heating (integrity) and the
**cloud API keys** (cost, abuse).

| Threat | Mitigation | Status |
|---|---|---|
| Eavesdropping or tampering on the network | TLS 1.2+ with server certificate verification (CA bundle); never `setInsecure`-style shortcuts | planned (step 3); the current echo build uses plain `ws://` on a LAN only |
| Rogue device or replayed session talks to the service | Per-device token, compared in constant time; 1 s delay after a failed attempt; token ≥ 16 chars enforced at start-up | done (server) |
| Cloud API keys leaked from the device | Keys live only on the server; the device knows only its own revocable token | by design |
| Secrets read from a lost device | Wi-Fi credentials and token in NVS, not in the firmware image; NVS encryption + flash encryption | planned |
| Malicious firmware on the device | Secure Boot v2 (signed images), signed OTA updates with rollback | planned, documented before enabling (eFuse changes are irreversible) |
| Resource exhaustion of the service | Message size limit (64 kB), 30 s utterance limit, read/idle timeouts, header timeout | done (server) |
| Path traversal via device name in saved recordings | `filepath.Base` on the device id | done (server) |
| Always-on microphone | Audio leaves the device only after local voice activity detection; the connection is opened per request; recording is shown by the green LED | partly done (VAD + LEDs) |
| Vulnerable dependencies | Pinned toolchains (ESP-IDF container tag, Go version), Dependabot, `govulncheck` in CI, GitHub Actions pinned to commit SHAs | done |

## Supply chain

* Firmware is built only in the official `espressif/idf` container with a fixed
  version tag, locally (`scripts/idf.sh`) and in CI.
* The server image is a static Go binary on `scratch`, running as an
  unprivileged user.
