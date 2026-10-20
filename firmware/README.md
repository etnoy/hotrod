# GARO IDVN ESP8266 bridge

Source-built replacement for the ESP8266 application. It joins an existing Wi-Fi network and exposes the controller's settings through a small local HTTP API. The STM32 controller continues to run the original firmware and manage the heater.

**Status: development firmware, compiled and tested offline; not yet tested on an appliance.** The current exported build has no Wi-Fi credentials or API token configured. It is a build-validation artifact, not a deployment image.

## Implemented

- ESP8266 station mode only; DHCP and automatic reconnect. No normal AP, web UI, cloud dependency, or captive portal.
- Normal GARO UART frames, CRC, state decoding, boot confirmation, and periodic keep-alive independent of network availability.
- Local authenticated HTTP reads and settings writes for mode, working window, heating curves, clock, and identification.
- Explicit asynchronous operation results, bounded inputs, controller freshness checks, and no automatic retry of an uncertain settings write.
- No controller flashing, firmware upload endpoints, ROM-loader driver, or recovery-system disablement.
- MQTT is deferred. The protocol library is independent of the HTTP/network layer so another transport can be added later.

This firmware exposes **configured modes and schedules**. It cannot read a temperature or measured relay state, set the local departure-time slots, or directly command arbitrary on/off with the controller command set reconstructed so far. A mode named `timer` still depends on the unit's local timers. No synthetic switch state is presented as a relay measurement.

## Configure and build

Copy `include/local_config.h.example` to `include/local_config.h`, then set SSID, password, and a unique API token of 24–128 bytes. Use C string escaping for quotes/backslashes in credentials. The file is ignored by Git.

Credentials are compiled into the ESP image; there is deliberately no network credential editor in this first version. Changing them requires rebuilding/programming. They use separate bounded strings, not stock `/config`; passwords may be 8–63 bytes, or empty for an open network. The bridge calls `WiFi.persistent(false)` so the connection code does not intentionally replace the stock SDK credentials. The device image itself still contains the configured secrets: keep configured binaries private.

From the repository root:

```sh
python3 -m venv analysis/.venv
analysis/.venv/bin/python -m pip install platformio==6.1.18
analysis/.venv/bin/python tools/build_firmware.py
python3 tools/test_firmware.py
```

The build downloads pinned dependencies on first use: PlatformIO ESP8266 platform 4.2.1, Arduino core 3.1.2, ArduinoJson 6.21.5. The native tests require clang++ with address/undefined-behavior sanitizers (or set `CXX` to a compatible compiler).

Build outputs:

- `build/garo-idvn-bridge.bin`: replacement image containing eboot and application.
- `build/garo-idvn-bridge.json`: SHA-256, source hashes, dependency versions, configuration-presence flag, and integrity checks.
- `firmware/.pio/build/garo_esp8266/firmware.elf`: symbols and sections for debugging.

`tools/build_firmware.py` only builds, verifies, and exports. It never uploads. Its integrity verifier accounts for Arduino 3's CRC32 metadata written after the ordinary application XOR checksum; treating the new image exactly like the stock Arduino 2.4.0 image would report a misleading XOR failure. Do not manually “repair” the finished application's XOR byte: that changes its full-image CRC32.

The selected 4 MiB DIO/40 MHz layout follows the stock image declaration. Actual flash capacity, PCB revision, pin routing, and preservation of the current filesystem/SDK/recovery areas require a physical dump and validation before selecting a programming procedure. Do not use stock firmware-update endpoints to install this development image on the assumption that their staging/rollback code supports it.

With blank configuration the image starts no AP, makes no configured station connection, and the protected API returns `503 api_token_not_configured` if reached. UART liveness is still serviced. There is no fallback AP: incorrect credentials require physical recovery/reprogramming.

## Controller link and GPIOs

UART0: 115200, 8N1, GPIO1 TX / GPIO3 RX. Runtime logs must never be printed to `Serial`; this implementation has none. The ESP ROM boot banner cannot be removed by suppressing application logs.

Stock startup writes GPIO5 low and GPIO4 high, then configures them as outputs. The replacement preserves those idle levels and never pulses a controller reset or enters its bootloader. The physical nets still need confirmation. `GARO_IDLE_GPIO4` and `GARO_IDLE_GPIO5` are board-qualification settings, not ordinary user controls.

The bridge sends startup confirmation (0x01) at boot and every 10 seconds, and reads configuration (0x28) every 15 seconds. It will continue doing so without a router, DHCP lease, HTTP client, or MQTT broker. Controller settings writes are allowed only after a confirmation and a fresh snapshot. Cached state expires after 45 seconds.

HTTP parsing handles at most 256 bytes per loop pass, up to 2 KiB headers/1 KiB bodies, with a 2-second total request deadline and bounded response-write timeout. It does not support uploads, chunked request bodies, HTTP keep-alive, or pipelining. This keeps incoming traffic from monopolizing the controller servicing loop.

## Local API

The router assigns the address. A DHCP reservation is a useful way to keep it stable. The configured hostname is a DHCP hostname; mDNS discovery is not implemented. The stock Android app still uses 192.168.20.1 and is not a client for this bridge.

All `/v1/` endpoints require:

```text
Authorization: Bearer YOUR_API_TOKEN
```

JSON writes also require `Content-Type: application/json` (an optional charset suffix is accepted). There is no TLS in the firmware; use it on a trusted local/IoT network. Tokens and credentials are never returned by the API.

- `GET /health`: unauthenticated minimal health/version, Wi-Fi status, controller availability, and configuration-present flags.
- `GET /v1/status`: IP/RSSI, heap, UART errors/timeouts, controller state age, latest operation result.
- `GET /v1/data`: fresh controller settings, clock, and versions. Returns 503 when no current snapshot exists. The data describes the last controller snapshot, with `state_age_ms`; it is not a live telemetry stream.
- `GET /v1/operation?id=N`: latest operation result. Only the latest operation is retained; an overwritten ID returns 410. IDs are per boot.
- `POST /v1/mode`: complete `{left,right,timers,period}`.
- `POST /v1/working`: complete `{days,fromTime,toTime,fromDate,toDate}`.
- `POST /v1/departure`, `POST /v1/reduction`: `{points:[[temp,minutes],...]}`.
- `POST /v1/time`: `{year,month,day,weekDay,hours,minutes,seconds}`.
- `POST /v1/identify`: no settings payload needed. This differs from stock's GET identify route.

Writes return HTTP 202 and an operation ID. **202 means queued, not applied.** Poll the result until `applied`, `rejected`, `timeout`, or `transport_error`. `queued`/`sent` are intermediate states. `applied` means the controller acknowledged acceptance; a separate read refresh follows. If the acknowledgment was lost, a timeout cannot establish whether the controller changed the setting. Read configuration before retrying. Only one user operation may be queued/in flight; another returns 409.

All fields of the selected settings block must be present; there is no merge with potentially stale cached fields. Values are range checked. Unknown top-level fields are ignored; the supported fields must still be complete and valid. There is no full-state POST, raw-command endpoint, Wi-Fi editor, clock-from-NTP behavior, or firmware updater.

Mode 0=departure, 1=reduction, 2=timer. Period 0=60 minutes, 1=30, 2=15. Curves require 2–16 ascending temperature points (-35..15 Celsius), with minutes 0–210 for departure and 0–60 for reduction. Clock weekday is Monday=1 through Sunday=7. Date/time validation is stricter than the stock MCU's byte checks.

## Client examples

The Python helper needs only the standard library. It polls operation results and exits unsuccessfully for rejected/uncertain writes. Set `GARO_URL` and `GARO_API_TOKEN` in the environment, then:

```sh
python3 tools/idvn.py status
python3 tools/idvn.py data
python3 tools/idvn.py identify
python3 tools/idvn.py sync-time
python3 tools/idvn.py mode --left departure --right timer --timers 1 --period 60
python3 tools/idvn.py working working.json
python3 tools/idvn.py departure departure.json
```

Mode changes and clock sync affect the controller. `sync-time` uses the automation host's configured local timezone. Scheduling periodic clock sync (and after DST changes) belongs to the home automation system in this first version.

Example `working.json`:

```json
{"days":127,"fromTime":{"hours":6,"minutes":10},"toTime":{"hours":7,"minutes":15},"fromDate":{"month":1,"day":1},"toDate":{"month":12,"day":31}}
```

Example `departure.json`:

```json
{"points":[[-20,120],[0,60],[10,0]]}
```

Those are examples of settings, not guaranteed remote on/off recipes. The controller still combines working windows, modes, curves, and local timers.

An optional [Home Assistant REST example](../examples/home-assistant.yaml) reads configured modes and defines explicit settings actions. It does not create a misleading relay switch or install anything into a live Home Assistant instance. Other automation systems can use the same JSON API or helper client.

## Verification and hardware bring-up

Native tests cover frame boundaries/CRC/noise/fragmentation, signed curve encoding, calendar/Sunday handling, snapshot offsets, matching replies, rejections, lost acknowledgments, absence of automatic write retries, lifecycle reset, and millis wraparound. The bounded HTTP parser is tested for header/body limits, duplicate lengths/auth, invalid requests, complete-body handling, and deterministic arbitrary-byte input. The actual JSON settings encoder is also tested, including missing fields, boolean/float/string values, integer overflow, and malformed curves; build once before these tests to obtain the pinned ArduinoJson headers.

For independent frame verification:

```sh
python3 -m venv analysis/.emulator-venv
analysis/.emulator-venv/bin/python -m pip install unicorn==2.1.4
analysis/.emulator-venv/bin/python tools/verify_stock_protocol.py
```

Use a current Python/pip on macOS for the prebuilt arm64 wheel. In this environment the emulator JIT required execution outside the filesystem sandbox. The comparison ran **136 cases against the original STM32 encoder/CRC machine code**, with payload sizes 0–128, and every generated wire frame matched. Only the final UART transmit function was stubbed. Results are in `analysis/stock-protocol-verification.json`; this validates framing, not a full simulated heater.

Before the first hardware run, retain a full ESP flash dump and verify isolation, fitted flash, pin mapping, and recovery access. The image never touches mains hardware directly, but its GPIOs communicate with a controller on the appliance board; do not attach a grounded programmer to potentially mains-referenced logic.

The first bench run should verify boot/confirmation and snapshot reads, remain stable beyond several 90-second recovery intervals, exercise absent/wrong Wi-Fi and router reboot, and compare one settings change at a time against the stock unit's display. Check normal local heater operation and whether the custom ESP image survives controller recovery logic. Keep the stock backup ready. The compiled image is not yet qualified for unattended heater operation.

## Upstream references

- [ESP8266 platform 4.2.1](https://github.com/platformio/platform-espressif8266/releases/tag/v4.2.1).
- [ESP8266 Arduino 3.1.2 station API](https://arduino-esp8266.readthedocs.io/en/3.1.2/esp8266wifi/station-class.html).
- [ArduinoJson 6.21.5](https://github.com/bblanchon/ArduinoJson/releases/tag/v6.21.5).
- [Home Assistant REST sensor](https://www.home-assistant.io/integrations/sensor.rest/) and [REST command](https://www.home-assistant.io/integrations/rest_command/).
