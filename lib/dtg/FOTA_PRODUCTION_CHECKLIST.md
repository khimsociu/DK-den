# DTG IoT 1.2.0 — Production FOTA + uplink period checklist

Agent cannot flash hardware. Run this on a real board after syncing the library.

## Build

1. Arduino-ESP32 3.x, partition scheme with dual OTA slots (e.g. Minimal SPIFFS 1.9MB APP with OTA).
2. Sketch calls `enableHubCmd()` + `enablePlatformFeatures()` (or factory NVS sketch).
3. Call `dtg.setFirmwareVersion(DTG_FIRMWARE_VERSION)` so Hub `up/info.fw` matches the sketch.
4. Never ship `DTG_OTA_TLS_INSECURE` / `DTG_OTA_ALLOW_HTTP`.

## Uplink period

| # | Scenario | Pass |
|---|----------|------|
| 1 | Online device: change period in Hub Information tab | `up/cfg_ack` ok; no-code / `DTG_EVERY(DTG_PERIOD_UPLINK)` / GPS cadence changes |
| 2 | Offline device: change period, then reconnect | Retained `full_sync` carries new `periodUplink`; NVS `pup` matches |
| 3 | Reboot after change | Period survives reboot (NVS `pup` / `pver`) |
| 4 | Stale retained `full_sync` with older version | Device keeps newer period (`pver` gate) |

## FOTA Baseline

| # | Scenario | Pass |
|---|----------|------|
| 1 | Upload `.bin` + trigger (Wi-Fi) | `downloading` → `installing` → reboot → `success`; `up/info.fw` new |
| 2 | Same on cellular PPP | Same; expect PPP buffer warnings, resume OK |
| 3 | URL trigger without checksum | API 400; device never starts |
| 4 | URL with wrong SHA-256 | `failed` / `checksum_mismatch`; no reboot |
| 5 | `http://` URL (lab build only) | `failed` / `http_not_allowed` |
| 6 | Pull antenna mid-download | Resume via Range; eventually success or fail cleanly |
| 7 | Flash image that cannot reach MQTT | After `DTG_OTA_CONFIRM_TIMEOUT_MS` (~8 min) rollback; Hub `ROLLED_BACK` |

## Notes

- First upgrade from pre-1.2.0: old firmware still reports `success` before reboot; Hub may miss a rollback once.
- Host tests: `tests/run_host_tests.ps1` (or CI `host-tests` job).
