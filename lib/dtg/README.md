# DTG IoT

Official **Arduino / ESP32 / ESP8266** client for **[DTG IoT Hub](https://dtgsoft.vn)**: MQTT telemetry, Hub downlink, optional Modbus RTU (ESP32), no-code I/O, and Hub FOTA.

Supports **Wi-Fi** (ESP32 + ESP8266) and **SIMcom A7680C-class** cellular (PPP, ESP32), plus **BN-880** GPS helpers (ESP32).

| | |
|---|---|
| **Version** | 1.2.0 (`library.properties`) |
| **Architectures** | `esp32`, `esp8266` |
| **License** | MIT |

---

## Features

- Production broker defaults: `mqtt.dtgsoft.vn` (TLS **8883**, plain **1883**) with Let's Encrypt roots **X1, X2, Root YR, Root YE** (`DTG_TLS_CA_BUNDLE`). Override with `dtg.setCaCert(pem)` for a private CA.
- Topic layout: `dtg/{projectId}/{gwAlias}/...` (MQTT token only in CONNECT - never in the topic)
- Telemetry publish, downlink commands, Hub `down/cfg` / `down/gw/caps`
- Hub gateway mode: no-code I/O + Modbus RTU (lazy via platform capabilities) + FOTA
- Watchdog, MQTT reconnect backoff, and cellular PPP recovery enabled by default after `begin()` / `beginCellular()`

---

## Requirements

- **Board:** ESP32 (Arduino-ESP32 core **3.x** recommended; cellular needs core PPP) or **ESP8266** (esp12e / NodeMCU; Wi-Fi + MQTT TLS + no-code + Hub cmd + FOTA)
- **Arduino IDE** 2.x or **PlatformIO**
- **Library Manager deps** (install if the IDE does not pull them automatically):

| Library | Required for |
|---------|----------------|
| **PubSubClient** | Always |
| **ArduinoJson** | Hub no-code / gateway / Modbus runtime |
| **TinyGPSPlus** | GPS examples only (ESP32) |

Modbus RTU is built in on **ESP32 only**. It uses the RTU client of [eModbus](https://github.com/eModbus/eModbus) 1.7.4 (MIT, see `src/modbus/emodbus/license.md`), vendored under the `dtg_emodbus` namespace so it does not clash with a separately installed eModbus. No extra install is needed and AsyncTCP is no longer required.

### ESP8266 notes

- Compile gate disables Modbus / Cellular / GPS (`DTG_FEATURE_* = 0`). Modbus profiles `#error`.
- TLS uses **BearSSL** + NTP time sync (required for cert expiry checks unless `tlsInsecure`).
- Credentials / Hub cfg / OTA pending state use a **LittleFS** Preferences shim (`/p/<ns>/<key>`). MQTT tokens are stored in plaintext on flash (same class of risk as ESP32 without flash encryption).
- Soft WDT is ~3.2s — long loops (OTA) feed the watchdog; default CPU target is **160 MHz**.
- **FOTA has no A/B rollback** on ESP8266. Prefer staged rollouts; a bad image can brick the device until serial reflash.
- Sample PlatformIO env: `examples/ESP8266/ESP8266_WiFi_Gateway/platformio.ini.sample` (`eagle.flash.4m2m.ld` + LittleFS).

---

## Install

### Arduino IDE

Public repo: [DTG-Corp/dtg-arduino](https://github.com/DTG-Corp/dtg-arduino). In Arduino IDE open Library Manager and search **DTG IoT**.

Zip install (fallback):

1. Zip this folder so that `library.properties` is at the **root** of the zip.
2. **Sketch → Include Library → Add .ZIP Library...**
3. Install **PubSubClient**, **ArduinoJson**, and **TinyGPSPlus** from Library Manager. Modbus works without any further library.

Examples appear under **File → Examples → DTG IoT → ESP32 → ...** and **ESP8266 → ...**

### PlatformIO

Copy `dtg` into the project `lib/` folder, or set `lib_extra_dirs` to the parent of `dtg`. Declare the same dependencies in `platformio.ini` / `lib_deps`.

---

## Hub credentials

Create a **Hardware gateway** in the Hub UI, then fill these in your sketch:

| Sketch field | From Hub |
|--------------|----------|
| `PROJECT_ID` | 5-digit project id (e.g. `12345`) |
| `GW_ALIAS` | Gateway alias (e.g. `gw01`) |
| `MQTT_USER` / `MQTT_PASS` | Same **gateway MQTT token** (both fields) |
| `CLIENT_ID` | `gw-` + gateway UUID |

Your sketch supplies Wi-Fi (or modem pins) and these values. Broker host, TLS CA, ports, and topic root live in the library (`dtg_config.h`).

---

## Quick start (Wi-Fi telemetry)

```cpp
#include <dtg.h>

DtgWiFi dtg("12345", "gw01", "TOKEN", "TOKEN", "gw-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");

void setup() {
  Serial.begin(115200);
  dtg.setLogStream(&Serial);
  dtg.begin("SSID", "PASSWORD", true /* TLS */, false /* insecure */);
}

void loop() {
  dtg.run();
  if (dtg.connected()) {
    dtg.publish("temperature", 23.5f);
  }
}
```

Prefer the polished examples (`ESP32_WiFi_Basic`, `ESP32_WiFi_Uplink`) for `DTG_EVERY` / `DTG_CONNECTED` patterns.

### Hub gateway (no-code + FOTA)

```cpp
#define DTG_WIFI_GATEWAY_ONLY 1   // optional: smaller flash (no cellular/GPS modules)
#include <dtg.h>

// ... credentials as above ...

void setup() {
  Serial.begin(115200);
  dtg.setLogStream(&Serial);
  dtg.begin(WIFI_SSID, WIFI_PASS, true);
  dtg.enableHubCmd();             // OTA + reboot commands
  dtg.enablePlatformFeatures();   // Modbus / no-code when Hub publishes caps
}

void loop() {
  dtg.run();
}
```

Do **not** call deprecated `enableModbus()` / `enableNoCodeIo()` on new sketches - use `enablePlatformFeatures()`.

### Cellular

See **ESP32_Gsm_Basic** / **ESP32_Gsm_Gps** (`DtgCellular`, modem pins, PPP). Same Hub credentials; transport differs.

---

## Examples

| Sketch | Purpose |
|--------|---------|
| `ESP32_WiFi_Basic` | Publish telemetry over Wi-Fi |
| `ESP32_WiFi_Uplink` | GPIO / button → Hub |
| `ESP32_WiFi_Downlink` | Receive a Hub value |
| `ESP32_WiFi_Listen` | `DTG_LISTEN` → drive GPIO |
| `ESP32_WiFi_Gateway` | **Reference Hub gateway** - no-code I/O + FOTA |
| `ESP32_WiFi_Modbus` | Gateway + Modbus RTU runtime |
| `ESP32_WiFi_Gps` | BN-880 GPS → `tele/_/gps_data` |
| `ESP32_Gsm_Basic` | Cellular MQTT (A7680C-class) |
| `ESP32_Gsm_Gps` | Cellular + GPS |
| `ESP32_TC5L_Modbus_Bare` | Modbus RTU bench only (no MQTT) |
| `ESP32_TC5L_Modbus_Gateway` | TC5-L pins + Hub Modbus gateway |

---

## Topics (cheat sheet)

| Plane | Example suffix | Role |
|-------|----------------|------|
| `tele` | `tele/_/temperature` | Telemetry (`publish("temperature", ...)` → device alias `_`) |
| `tele` | `tele/modbus/{ch}/{dev}/{tag}` | Modbus tag values |
| `up` | `up/status`, `up/info` | Online / device identity |
| `down` | `down/cfg`, `down/gw/caps`, commands | Hub → device |

GPS helper: `publishGps(...)` → `tele/_/gps_data`.

---

## Debugging

```cpp
#define DTG_DEBUG 1   // before #include <dtg.h> - sketch-side only
#include <dtg.h>

void setup() {
  Serial.begin(115200);
  dtg.setLogStream(&Serial);   // library MQTT / transport logs
  dtg.setBootStream(&Serial);  // optional startup banner
}
```

---

## Troubleshooting

| Symptom | What to try |
|---------|-------------|
| **Sketch too big** | **Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP with OTA)** (or Huge APP without OTA). Or `#define DTG_WIFI_GATEWAY_ONLY 1` before `#include <dtg.h>`. |
| **Multiple libraries found for `PPP.h`** | Remove a third-party `PPP` folder under *Documents/Arduino/libraries* so the **ESP32 core** PPP is used. `PPP.h` is part of Arduino-ESP32 core 3.x, not a Library Manager package. |
| **PlatformIO: `PPP.h: No such file`** | A PPP library installed in the Arduino IDE sketchbook is not visible to PlatformIO. Wi-Fi sketches compile without it. Cellular needs a PlatformIO platform that ships Arduino-ESP32 **core 3.x** (that core contains `PPP.h`). |
| **`DtgCellular` does not name a type** | Cellular sketches must `#include <PPP.h>` before `#include <dtg.h>`. `PPP.h` is not on the include path until the sketch includes it, so the library cannot see it from inside a header check. |
| **MQTT connects but no data** | Confirm token, `CLIENT_ID` (`gw-<uuid>`), project id, and alias match the Hub gateway. |
| **Modbus bus timeout** | Validate RS485 wiring with `ESP32_TC5L_Modbus_Bare` before enabling MQTT. Default UART pins are in `dtg_config.h` (`DTG_MODBUS_UART*`); Hub may override `rxPin` / `txPin` / `dePin` / `rePin`. |
| **RAM / String pressure** | Prefer `F("...")` / `snprintf` over `String` concat in `loop()`. |

---

## Advanced

### Flash profiles (compile-time)

Set **one** profile before `#include <dtg.h>`. On Arduino 2.x / ESP32, also add `build_opt.h` next to the sketch so library `.cpp` files see the same flags (Hub export does this automatically).

| Macro | Includes |
|-------|----------|
| `DTG_PROFILE_MQTT_ONLY` | Core + hub cmd |
| `DTG_PROFILE_MODBUS_CAPABLE` | + Modbus (runtime via caps) |
| `DTG_PROFILE_NOCODE_CAPABLE` | + No-code I/O |
| `DTG_PROFILE_HYBRID_CAPABLE` | Modbus + no-code |

### Resilience (defaults after `begin` / `beginCellular`)

- **Task watchdog** - feeds from `run()` (default 120 s). Override: `#define DTG_WATCHDOG_TIMEOUT_S 180`. Disable: `dtg.enableWatchdog(0)`.
- **MQTT reconnect** - exponential backoff + jitter (`DTG_MQTT_BACKOFF_*` in `dtg_config.h`).
- **Cellular PPP recovery** - periodic retry; hard reset after `DTG_PPP_HARD_RESET_AFTER` failures.

### Wi-Fi provisioning (SoftAP)

Hold **BOOT** ~20 s, or leave the device offline for `DTG_WIFI_OFFLINE_AP_MS` (default 3 minutes). The radio opens `dtg-gw-xxxxxx` with a **per-device random PSK** stored in NVS (printed once on the boot banner / `up/info`).

- Default `DTG_WIFI_AP_PSK` is empty: first boot generates a 16-char PSK and keeps it.
- Set `DTG_WIFI_AP_PSK` only as a factory seed; it is written to NVS when the slot is empty and is not re-applied on later boots.
- Portal is at `http://192.168.4.1/` (Cache-Control: no-store). Change primary/backup STA credentials there.

### TLS / OTA (Baseline production)

- MQTT 8883 and HTTPS OTA share `DTG_TLS_CA_BUNDLE` (or `setCaCert()`).
- Hub `start_ota` requires HTTPS URL and `checksum` (`sha256:<64 hex>`). Empty/invalid checksum or `http://` → `failed` / `bad_checksum` or `http_not_allowed` (no reboot).
- Download supports HTTP `Range` resume across transient PPP drops (`DTG_OTA_RESUME_ATTEMPTS`).
- After flash the device reports `installing`, reboots, then self-tests: MQTT reconnect confirms the image (`success`) or timeout rolls back (`rolled_back`). Requires dual-OTA partitions + `CONFIG_APP_ROLLBACK_ENABLE`.
- Override reported `up/info` fw with `dtg.setFirmwareVersion("1.2.0")` (sketch `#define DTG_FIRMWARE_VERSION` does not reach library `.cpp`).
- Lab only: `#define DTG_OTA_TLS_INSECURE 1` / `DTG_OTA_ALLOW_HTTP 1` in `build_opt.h` (never ship).

### Uplink period (web → device)

- Hub UI sets gateway `periodUplinkMs`; MQTT `down/cfg` actions `set_period_uplink` / `full_sync.periodUplink`.
- Library core (`dtg_core_cfg`) always subscribes `down/cfg` — works with or without no-code GPIO.
- `DTG_EVERY(DTG_PERIOD_UPLINK)`, no-code sampling, and `DtgGps` (default) follow that period. `DtgGps::setPublishIntervalMs(ms)` freezes GPS cadence.

### API notes

- One global client instance per sketch (examples use `dtg`).
- Optional broker override: `dtg.setMqttBroker(host, port)` or `dtg.setBrokerIpv4("x.x.x.x")` (plain MQTT; TLS + raw IP is awkward).
- `publish()` `ts` is **uptime seconds** for local debug. Hub ingest uses server time.
- GPS helper `publishLogCoord` uses Hub topic `log_toado` (contract; do not rename in the library).

---

## License

MIT - see [`LICENSE`](LICENSE).
