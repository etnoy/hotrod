# GARO IDVN firmware investigation

Investigation started 2026-10-20. This is a living notebook. Statements below distinguish observations from hypotheses; nothing has been flashed to a device.

## Scope and preservation

- The source APK under `original/` is read only for this investigation. Do not edit, rename, replace, extract into, or generate files inside that directory.
- Work from `analysis/Garo-IDVN_1.0.3.apk`. Extraction is under `analysis/extracted/`.
- First milestone: make the Wi-Fi module join an existing wireless network (station/client mode). MQTT is a later milestone.
- Preserve controller behavior while investigating Wi-Fi changes. A controller patch is not presently necessary for the network-mode goal.
- An earlier LLM investigation was supplied by the user. Its scripts have been preserved under `analysis/prior-analysis/`, with provenance. Treat them as leads until independently verified.

## Verified input and extracted images

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `original/Garo+IDVN_1.0.3_APKPure.apk` | 4,012,779 | `c67728101c2272043de95f2a1a1fbc46293befea68b285224c7bea3c9c24877a` |
| `assets/www/fw/wifi.bin` | 323,968 | `2ae59d60c814fcb47f688c26ee70c7fb762b3ca5a40584115f5ffed0687caddb` |
| `assets/www/fw/mcu.bin` | 59,333 | `ee0048cfdefe9895c5824e6b5387a0ae7ca05d9076fc2c66e70502b2c2b17ba0` |

`analysis/source-provenance.json` records the source/copy hash. `analysis/apk-inventory.json` lists all 464 APK entries. Printable-string reports retain binary file offsets in `analysis/wifi.bin.strings.txt` and `analysis/mcu.bin.strings.txt`.

The APK contains a Cordova web app with readable application JavaScript in `assets/www/`. Firmware extraction requires ZIP access, not Android bytecode decompilation. The JavaScript reports app version `1.0.2`, controller version `100`, and Wi-Fi version `100`, despite the APK filename containing `1.0.3`. Do not equate the APK filename with the bundled firmware revision.

The earlier analysis's two firmware files were compared byte for byte against this extraction and match.

## First platform observations

- `wifi.bin` starts with ESP image magic `e9`; the initial header has one segment and entry point `0x4010f29c`. It contains an initial loader and another image at offset `0x1000`. Both segment maps and checksums have now been verified below.
- Wi-Fi strings include build paths identifying ESP8266 Arduino core **2.4.0**, and an SDK banner containing a 2017 build timestamp. That identifies linked software, not the date of this product firmware build.
- `mcu.bin` starts with an ARM-style vector table: initial stack pointer `0x20001b70`, reset vector `0x0800e5ed` (Thumb entry `0x0800e5ec`). This supports a Cortex-M firmware image at `0x08000000`. Exact chip and board revision still need confirmation.
- GD25Q32C diagnostic strings are present in the Wi-Fi image. Their presence alone does **not** establish the actual flash manufacturer fitted to the hardware.

## Verified app behavior

Source paths here are relative to `analysis/extracted/assets/www/`.

- `script.js`: hard-coded API base `http://192.168.20.1/v1`.
- `wifi.js`, `data.js`: Wi-Fi form sends `ssid`, `password`, `visible`; there is no station/AP selector or separate router-credential form.
- `script.js:setTime()`: sends `Date.getDay()` (Sunday = 0). The controller rejection of zero was subsequently confirmed in the validator, documented below.
- `firmware.js`: reads `fw/mcu.bin` or `fw/wifi.bin`, builds multipart upload with field `file`, and posts to `/v1/upload`. Then posts `{filename, crc}` to `/v1/update/controller` or `/v1/update/wifi`; polls the corresponding GET endpoint every 2 seconds.
- `firmware.js:getCrc()`: starts at zero and iterates a CCITT-polynomial CRC routine. Its output on both stock files was subsequently checked against CRC-16/XMODEM, documented below.
- Firmware update UI compares versions and offers a reflash path. App-side CRC checks do not prove the device lacks signature/authenticity checks.

## Initial investigation questions and current answers

1. Does startup force AP-only mode? The traced AP startup preserves the STA bit; the shared stop path explicitly selects Wi-Fi off.
2. Which addresses implement AP startup/mode switching? Mapped below. Station association/provisioning still needs validation.
3. What are the segments and checksum rules? Verified below, with regenerable image maps.
4. What UART/recovery behavior must a replacement retain? Command sizes and selected lifecycle behavior are mapped; complete framing/startup behavior remains open.
5. What settings and recovery access need backup? Device-specific flash contents and physical access remain hardware prerequisites.

## Limits and hardware dependencies

No device is connected, no HTTP calls have been sent to an outlet, and no firmware or settings have been changed. ROM recovery, GPIO assignments, actual flash capacity, mains isolation, and controller model are unverified hardware claims. Before physical programming, establish isolation and recovery access; a bench programmer must not be attached to potentially mains-referenced logic.

Do not treat automatic rollback as a guaranteed recovery method, and do not change STM32 option bytes/readout protection as part of this work.

## ESP8266 image format and memory map — verified

The file contains two independently checksummed `e9` images. All offsets in this section are hexadecimal and refer to the extracted `wifi.bin`, not the compressed APK.

| Image | Header offset | Entry point | Checksum offset | Calculated / stored |
| --- | --- | --- | --- | --- |
| Initial loader | `0x000000` | `0x4010f29c` | `0x00057f` | `0x2d / 0x2d` |
| Application | `0x001000` | `0x40100004` | `0x04f17f` | `0x03 / 0x03` |

| Image / segment | File data offset | Load / mapped address | Length |
| --- | --- | --- | --- |
| Loader segment | `0x000010` | `0x4010f000` | `0x568` |
| App: flash-mapped code and constants | `0x001010` | `0x40201010` | `0x453b0` |
| App: instruction RAM | `0x0463c8` | `0x40100000` | `0x70fc` |
| App: data | `0x04d4cc` | `0x3ffe8000` | `0x554` |
| App: data / read-only constants | `0x04da28` | `0x3ffe8560` | `0x174c` |

The map is also machine-readable in `analysis/wifi-image-map.json`. Addresses in uninitialized RAM, such as `0x3ffef738`, have no corresponding bytes in this update image. A pointer to such an address is useful evidence; trying to read its initial contents from the file is an error.

Each image checksum starts with `0xef` and XORs the bytes in all its segment payloads. The checksum byte is at `end_of_last_segment | 0x0f`. The application ends exactly after its checksum at file length `0x4f180`. Padding between the initial loader and the app is not part of the app's checksum. A code patch needs the checksum for the containing image recomputed, in addition to any outer transfer CRC.

Both headers encode flash mode `2` and size/frequency byte `0x40`, conventionally ESP8266 DIO, 4 MiB, 40 MHz. This is the **image's declared flash setup**, not a measurement of the fitted flash. The APK image covers only `0x4f180` bytes; it is not a full flash backup and contains no complete filesystem/SDK settings dump.

Do not apply the simple `address - 0x40200000` conversion to every segment. It works for this image's flash-mapped segment; RAM segments need the table above.

## Wi-Fi startup and shutdown — verified disassembly

Function names in the following table are analyst-assigned. The binary has no original symbol table. Arduino library names are matched using the exact 2.4.0 source and the observed instruction behavior.

| Address | Assigned identity | Evidence / role |
| --- | --- | --- |
| `0x4020b3f0` | Application setup | Initializes runtime, serial, filesystem/config, HTTP handlers, update state, then AP |
| `0x40206eb8` | Start AP | Loads the AP IP, configures AP network, starts softAP |
| `0x40206f64` | Stop AP / Wi-Fi | Calls softAP disconnect and then `WiFi.mode(0)` |
| `0x40206f9c` | AP housekeeping | Deadline and deferred restart handling |
| `0x40207070` | Save app Wi-Fi configuration | Writes 67 bytes to `/config` |
| `0x402070b8` | Factory Wi-Fi defaults | Builds SSID, default password, visibility and writes config |
| `0x40207110` | Load app Wi-Fi configuration | Reads `/config`, checks size/version; falls back to defaults |
| `0x4020a488` | HTTP Wi-Fi settings handler | Parses SSID/password/visibility and schedules AP restart |
| `0x4020b470` | `softAPConfig`-like wrapper | Calls `enableAP(true)` before AP IP setup |
| `0x4020b538` | `softAP`-like wrapper | Calls `enableAP(true)`, validates and configures AP |
| `0x4020b6e8` | `softAPdisconnect`-like wrapper | Disconnects AP; optional AP mode removal |
| `0x4020b7bc` | `WiFi.mode` | Selects persistent vs current SDK mode setter |
| `0x4020b80c` | `WiFi.enableAP` | Preserves other mode bits when enabling/disabling AP |
| `0x40238894` | SDK mode getter | Used by Arduino mode helpers |
| `0x40238b20` | SDK persistent mode setter | Selected when Arduino persistence flag is true |
| `0x40238b34` | SDK current-only mode setter | Selected when persistence flag is false |

### What startup actually does

`0x4020b3f0` calls these routines, in order:

```text
0x40207f9c  runtime/timer/GPIO setup
0x402080fc  serial setup
0x40207208  filesystem-related setup
0x40207110  load config
0x40208dc8  register/start HTTP service
0x4020755c  initialize controller-update state
0x40206eb8  start AP
```

AP startup loads bytes `c0 a8 14 01` at `0x3ffe8570`: **192.168.20.1**. It constructs an all-255 subnet mask and configures the AP IP/gateway. It passes AP channel **1**, hidden flag **0**, and maximum clients **4**. If the saved password is empty, it passes a null password pointer; otherwise it uses the saved password.

The visibility option is not the `ssid_hidden` argument here. At this call site the network is broadcast. Instead, `visible` controls whether the AP receives a shutdown deadline. This explains why the app calls the option **Always Visible**.

The Arduino `enableAP` helper reads the current mode, tests the AP bit, and uses `current_mode | 2` when adding AP. It does **not** clear the station bit. The AP wrappers both call this helper. Therefore, a saved station configuration is a plausible way to boot the unmodified application into AP+STA. This is a verified code property, not proof of successful association on hardware.

### The important shutdown complication

The stop routine explicitly does the equivalent of:

```cpp
WiFi.softAPdisconnect(true);
WiFi.mode(WIFI_OFF);
```

The final mode call is at `0x40206f8d`, with mode zero supplied at `0x40206f8b` by `mov.n a3, a12`; `a12` was set to zero. Thus **a running station connection is also shut down** when this routine runs.

Known callers include AP timeout, deferred settings restart, and other runtime events. Direct-call sites found for this routine: `0x40206fc2`, `0x40207008`, `0x40208314`, `0x4020832e`. Raw cross-reference searches are candidate generators; the surrounding decoded code was inspected for these sites.

The deadline is initialized to current counter + `0x4650` (18,000). A timer callback runs with an interval argument of 5 and increments that counter every second callback. Under the expected millisecond timer interpretation this is about **180 seconds**, not 18 seconds. The exact wall-clock timing still needs a hardware measurement/library timer match. This AP visibility deadline is separate from the controller's Wi-Fi recovery watchdog.

When no client is associated at deadline expiry, the AP housekeeping path calls the stop routine. When there are clients it pushes the deadline forward by `0x3e8` counter units. A deferred settings change also stops and restarts AP, so setting `visible=true` alone does not address every way that station mode could be lost.

### Implication for the earlier no-patch proposal

The proposal to provision SDK station credentials with a temporary sketch and restore stock firmware has partial static support: stock AP startup preserves STA. However:

1. Saved credentials, SDK auto-connect state, flash layout compatibility, and actual association have not been tested.
2. Stock AP timeout/reconfiguration can shut down the station interface.
3. A temporary sketch does not speak the controller protocol; it can trigger reset/recovery behavior if left running on the assembled unit.
4. Reflashing only the stock application's region would need to preserve the correct filesystem and SDK sectors. The APK alone cannot establish the unit's current partition contents.

Treat this as a future bench experiment with a full dump, not a confirmed solution.

## App Wi-Fi configuration structure — verified offsets, inferred field layout

The application writes exactly **67 bytes (`0x43`)** from RAM base `0x3ffef738` to `/config`. Loading requires that length and a first byte equal to 1. The field offsets are supported by default initialization and the HTTP setter:

| Offset | Bytes | Interpretation |
| --- | ---: | --- |
| `0x00` | 1 | Structure version / magic, set to 1 |
| `0x01` | 1 | Controller-recovery counter/state; default 4, not a station-mode setting |
| `0x02` | 32 | AP SSID storage at `0x3ffef73a` |
| `0x22` | 32 | AP password storage at `0x3ffef75a` |
| `0x42` | 1 | Always-visible flag; default 0 |

The factory SSID generator formats `"%s%02x%02x"` with prefix `GARO-IDVN-` and MAC bytes 4 and 5. This confirms a four-digit lowercase hexadecimal suffix from the last two bytes of the MAC returned by the AP MAC wrapper. The default password is copied from `garo-idvn`.

**Potential existing bug:** `wifi.js` permits passwords up to 63 characters; this stored field is only 32 bytes. The HTTP setter calls a null-terminated copy-like function at `0x402127a0` with destination/source, without a length argument, for both SSID and password. Its implementation has the structure of optimized `strcpy`. This suggests an overflow for overlong input and an overlap with the visibility byte at 32 password characters. This is static evidence, not a tested exploit. Do not use long credentials to probe it on the appliance. A custom station configuration must have explicit lengths, separate fields, and bounds checks rather than reusing this layout for router passwords.

## Checksums and update packaging — verified offline

Two different integrity mechanisms must not be confused:

1. **ESP image checksum:** one XOR byte inside each `e9` image, described above.
2. **App upload/update CRC:** CRC-16/XMODEM, polynomial `0x1021`, initial value zero, non-reflected, no final XOR, over the complete binary file as uploaded.

The actual `firmware.js` was evaluated in an isolated Node VM; its `getCrc()` result matched Python `binascii.crc_hqx(data, 0)` on both extracted firmware files:

| File | CRC decimal | CRC hexadecimal |
| --- | ---: | --- |
| `wifi.bin` | 62581 | `0xf475` |
| `mcu.bin` | 919 | `0x0397` |

The app sends the CRC as a JSON number. It uploads the raw binary inside multipart form data, then passes the uploaded filename and CRC to a separate update endpoint. It uses `controller` in the URL for `mcu.bin`, not `/update/mcu`.

The app's state constants are: controller init `0x10`, start `0x11`, initial reset/unreset `0x12/0x13`, final reset/unreset `0x14/0x15`, wait `0x16`, completed `0x20`, end `0x21`, error `0x22`, restore `0x23`; Wi-Fi init through error `0x01` to `0x08`. The same small numeric values occur in other protocols; do not assume all state constants are controller UART commands.

At the end of the initial extraction pass, no modified image had been produced or installed. A replacement development build has since been created, described in the update at the end of this notebook. Finding XOR/CRC checks does not by itself establish that all update paths lack other validation. Candidate-image acceptance, staging, rollback-copy selection, and confirmation still require hardware/update-path qualification before installing a custom build.

## Controller firmware — independently verified command dispatch

The image is Thumb code at `0x08000000`. Peripheral literals include the STM32F0-style GPIO bank range `0x48000000` and USART/SPI register ranges, consistent with the prior Cortex-M0/STM32F0 identification. This is still insufficient to name an exact STM32 part. 59,333 bytes of firmware does not prove that the chip has only 64 KiB of flash.

The dispatcher starts at `0x08005ca4`. It reads the command at buffer offset 2, subtracts 1, bounds-checks against `0x31`, and jumps through a 50-entry halfword table at `0x08005ccc` using `add pc, r1` at `0x08005cc8`.

Separate command-length tables are at `0x08009978` and `0x080099e0`, with dispatch instructions at `0x08009976` and `0x080099de`. These independently confirm request and response payload lengths:

| Command | Request bytes | Response bytes | Handler | Interpretation |
| --- | ---: | ---: | --- | --- |
| `0x01` | 0 | 0 | `0x08005d30` | Confirm successful Wi-Fi startup/recovery |
| `0x06` | 1 | 1 | `0x08005d4a` | Wi-Fi/controller interaction state control |
| `0x11` | 0 | 1 | `0x08005d52` | Activity / keep-alive indication |
| `0x13` | 0 | 1 | `0x08005d5a` | Identify indication |
| `0x15` | 7 | 1 | `0x08005d62` | Validate and set clock |
| `0x21` | 4 | 1 | `0x08005d86` | Write mode block |
| `0x23` | 9 | 1 | `0x08005d94` | Write working window |
| `0x25` | 32 | 1 | `0x08005da2` | Write departure curve |
| `0x27` | 32 | 1 | `0x08005db0` | Write reduction curve |
| `0x28` | 0 | 91 | `0x08005dca` | Read combined configuration/time/version/storage data |
| `0x29` | 77 | 1 | `0x08005e68` | Write combined configurable blocks |
| `0x30` | 0 | 1 | `0x08005eaa` | Begin Wi-Fi image staging transfer |
| `0x31` | 128 | 1 | `0x08005edc` | Write image transfer block |
| `0x32` | 6 | 1 | `0x08005efe` | Finalize image transfer metadata |

All other entries in this dispatch table lead to the default case. This describes this firmware's ordinary command dispatcher, not the STM32 ROM bootloader command set or every possible unsolicited controller message. Response `0x05`, for example, is constructed in another path.

`analysis/mcu-command-map.json` regenerates the numeric table directly from the binary; command interpretations come from handler behavior and the app-facing paths.

### Combined configuration layout

The 77-byte write payload consists of mode (4), working window (9), departure curve (32), reduction curve (32). The read response begins with those same 77 bytes, followed by clock (7), then 7 bytes of version/storage information, for 91 total. This rules out treating `GET /v1/data` as a temperature/relay-state telemetry dump.

The read handler emits constants `0x0068` and `0x0064` after the clock. The subsequent reconstruction correlated these positions with the ESP JSON serializer: **hardware version 104**, **controller firmware version 100**. They are separate fields, not conflicting controller revisions.

There is no departure-time payload in this mapped command set. A custom ESP alone can expose the existing settings over another transport, but cannot use these commands to set local departure timer slots. The prior report's specific timer RAM address, slot layout, button-handler writer analysis, and claims about daily repetition have **not** been independently re-proven here.

## Serial link and recovery — verified parts and remaining gaps

The ESP serial initializer at `0x402080fc` supplies baud **115,200** and normal-operation format constant **`0x1c`** to a HardwareSerial-like initializer. The controller update path supplies **`0x1e`**, consistent with the earlier report of normal 8N1 and STM32 ROM-loader 8E1. The bytes, values, and distinct modes are verified; exact pin mapping and electrical wiring need board inspection.

The ESP's initialization also configures GPIOs 4, 5, and 2, and its controller-programming sequence manipulates 4/5. This supports the reported reset/boot-control wiring but does not prove which board net reaches STM32 NRST or BOOT0.

Three protocols must be kept separate:

- Normal app/controller messages: controller command table above, CRC-bearing frames. `0x080060ac` computes CRC starting at zero over command then payload; `0x08006124` appends the low byte then high byte and sends a length-adjusted buffer.
- ESP to STM32 ROM updater: the ESP contains a `0x7f` sync and command/complement sender at `0x40207470`; it changes UART format during programming.
- STM32 to ESP ROM updater: controller routines around `0x08009a34` implement `c0` delimiters and `db dc` / `db dd` escaping, plus an `0xef`-initialized XOR in the bootloader transaction path. **That is not evidence that ordinary controller messages use SLIP or XOR.**

The initial pass left normal framing unresolved. Subsequent tracing established a zero start byte, length N+4, command, N payload bytes, and little-endian CRC16 over command/payload. This is now documented and independently verified against the MCU encoder in [GARO-PROTOCOL.md](GARO-PROTOCOL.md). Less-used events and some state/error meanings still need capture/analysis.

### Controller watchdog and rollback

At dispatcher entry `0x08005cae`, a time getter is called and **90,000 (`0x15f90`) ticks** is added to a deadline at `0x2000046c + 0x18`. The runtime path around `0x080061d4` checks that deadline. On expiry in relevant states it increments a recovery counter and takes reset/reflash branches. Assuming the expected millisecond clock, this is the reported **90-second** watchdog; physical timing is not measured.

Recovery branches use names `wifi`, `wifi0`, `wifiT`; the ESP's mirrored controller-recovery code uses `/mcu`, `/mcu0`, `/mcuT`. These are **different chips' recovery images**, not interchangeable filenames. The loader also references `/mcu1` in a config-fallback path and `/mcuI` in update initialization.

Command `0x01` checks the recovery counter/state byte at `0x200016ac + 2`, performs file/state operations, clears it, and saves state. The deadline is refreshed before command dispatch, but an arbitrary malformed UART byte stream will not necessarily reach this point: framing/validation still matters.

A future replacement must maintain valid controller communication while joining Wi-Fi, including when the router is absent. Do not block startup in an indefinite `while (WiFi.status() != WL_CONNECTED)` loop. Boot confirmation and keep-alive must be local lifecycle behavior, independent of HTTP clients or a later MQTT broker.

The prior claim that both chips are always recoverable through ROM is too broad as an operational guarantee. Recovery also depends on accessible pins, board wiring, power/isolation, readout/boot configuration, correct flash contents, and functioning hardware. None is established by this APK.

## HTTP API reference and evidence level

The route-registration literal pool begins at `0x40208d50`; registration code begins at `0x40208dc8`. The following routes are present. Methods and JSON request fields are corroborated by application code; absence of other routes has not been established by a complete control-flow proof.

| Method used by app | Path | Purpose / body |
| --- | --- | --- |
| GET | `/` | Device HTML page; handler `0x40208844` |
| GET | `/v1/identify` | Identification; handler `0x40208734` |
| POST | `/v1/wifi` | `{ssid,password,visible}`; handler `0x4020a488` |
| POST | `/v1/time` | `{year,month,day,weekDay,hours,minutes,seconds}`; handler `0x4020a3f4` |
| POST | `/v1/mode` | `{left,right,period,timers}`; handler `0x4020a384` |
| POST | `/v1/working` | Days, from/to times, from/to dates; handler `0x4020a370` |
| POST | `/v1/departure` | `{points:[[temperature,minutes],...]}`; handler `0x4020a354` |
| POST | `/v1/reduction` | Same point shape; handler `0x4020a54c` |
| GET / POST | `/v1/data` | Combined state / preset write; multiple callbacks registered |
| POST | `/v1/upload` | Multipart firmware upload; upload callbacks registered separately |
| GET / POST | `/v1/update/wifi` | Status / start update; handler `0x4020a274` |
| GET / POST | `/v1/update/controller` | Status / start update; handler `0x4020a14c` |

The JSON response helper at `0x40208784` sends `Access-Control-Allow-Origin: *` and content type `application/json; charset=utf-8`. The stock app sends no login credentials. This does not establish that every route has identical authorization behavior, but it matters when moving the existing service onto a LAN: the service should be assumed reachable by peers on that LAN unless separately restricted.

The app's state object includes `wifi`, `version`, `fs`, `mode`, `working`, `departure`, `reduction`, `time`. Example values in the earlier LLM report are illustrative, not a captured response from this unit.

Mode labels in the supplied app are Departure, Reduction, and Charging/Timer (Swedish Timer). The earlier summary's working-window boundary rules, `days=0` special case, flat-curve on/off workarounds, and local timer repetition remain leads requiring separate verification. They are unnecessary for the immediate network-mode milestone, so no operational scripts using those assumptions have been created.

### Confirmed Sunday clock bug

Controller validator `0x080087e4` rejects a zero fourth byte (weekday) and rejects values >= 8. Therefore the accepted weekday range is **1–7**. The app sends JavaScript `getDay()`, whose Sunday value is **0**, so its Sunday clock payload is rejected by this controller path. The prior report of this bug is independently confirmed.

A future clock sender should use `((getDay() + 6) % 7) + 1` for Monday=1 through Sunday=7. The validator also checks month/day/year and time-byte ranges; it should not be treated as a full calendar-validity checker.

## Concrete path to station/client mode

### First milestone acceptance criteria

The intended outcome is that the outlet joins an existing Wi-Fi network and its current API works at a LAN address, while the controller continues its normal behavior. MQTT is excluded from this milestone.

1. Station credentials are stored in a new, bounded, versioned format, separate from the stock AP settings.
2. Association uses DHCP initially; find the address through the router. A hostname or mDNS may be added separately after a stable connection works.
3. Router unavailability, bad credentials, or DHCP timeout must not stop UART servicing, controller confirmation, or keep-alive.
4. AP shutdown, settings changes, idle timeout, and physical Wi-Fi control must not accidentally disable the station interface.
5. Cold boots, router reboot, association failure/retry, and a run longer than several controller recovery intervals must retain the custom firmware and normal controller behavior.
6. Demonstrate a read-only `/v1/data` request at the LAN address. The stock app's fixed `192.168.20.1` URL will need an eventual client-side change or another client; station support alone does not make that Android UI discover the new address.
7. Recovery and rollback are verified before depending on a custom image; keep the stock firmware plus full device dumps.

### Option A: retain stock ESP application and patch network lifecycle

This preserves the existing HTTP API and serial/update behavior. A robust patch must address provisioning and **both** AP start/stop paths, not just change an opmode byte.

A concrete instruction-level candidate for the stop-path component is:

```text
virtual address / file offset: 0x40206f8b / 0x006f8b
stock bytes:                   3d 0c   ; mov.n a3, a12 (zero here)
experimental alternative:       0c 13   ; movi.n a3, 1 (WIFI_STA)
following call:                        WiFi.mode at 0x4020b7bc
```

This would leave STA selected after the AP disconnect step, rather than all Wi-Fi off. It does **not** provision router credentials, guarantee connection, disable AP at initial startup, or solve the whole milestone. It also changes behavior for every caller of the shared stop routine. It is recorded as a reviewable patch hypothesis, **not a ready-to-flash firmware**; no bytes in the extracted image have been changed.

The stock AP-only-to-STA transition still needs saved station credentials and association behavior validated. Pure station startup would need a separate startup replacement/hook that avoids re-enabling AP. Use AP+STA only as a deliberate bring-up stage; the requested final network behavior is station/client mode, with fallback AP policy explicitly designed.

Any patch builder should verify the exact stock SHA-256 and expected old bytes, write a new path, recompute the appropriate ESP checksum and whole-file CRC, and emit an edit manifest with addresses, before/after bytes, and new hash. Never edit the only working stock extraction in place.

### Option B: replace ESP application and preserve the controller protocol

This gives maintainable source and a straightforward later MQTT integration, but first requires a complete serial protocol implementation. A generic ESPHome/Tasmota image flashed without a GARO controller driver would not preserve the observed recovery lifecycle.

Implement UART framing/checksums, request/response matching, unsolicited events, boot confirmation, keep-alive, state handling, and recovery interaction before adding networking changes. Keep network handling nonblocking. Start with read-only queries, compare with the stock app, then add configuration writes.

No reason has been found to patch controller firmware for station mode. Adding departure-time commands or temperature/relay telemetry would be a separate controller-facing scope with separate validation.

### Deferred MQTT milestone

After station mode is stable, define MQTT topics and state semantics around actual capabilities. Distinguish configuration acknowledgments from measured relay state; the present API does not supply the latter. Avoid translating a generic MQTT switch into an assumed hardware on/off command. Broker availability must not become a controller liveness requirement. No MQTT implementation has been started.

## Hardware information and backups needed for the next phase

The supplied prior investigation suggests ESP8266 plus STM32F0, UART sharing, external controller SPI storage, and reset/boot control wiring. Before selecting a flash/recovery procedure, record:

- Product/PCB revision and clear chip/module markings.
- ESP flash JEDEC ID and detected capacity; dump the entire detected flash, preserving filesystem, configuration, calibration, SDK settings, and recovery images.
- STM32 exact part, accessible SWD/UART/BOOT/reset pads, and current protection configuration. Reading identity/protection information is different from changing protection or erasing flash.
- Whether the low-voltage logic is isolated from mains and how it can be powered safely on the bench.
- Verified reset and boot signal routing between the chips; do not assume GPIO4/5 roles solely from a software sequence.
- Whether another chip can drive the same UART during external programming, and how to isolate that contention.
- Boot and idle UART captures followed by one stock app request at a time, with timestamps and both directions recorded.
- Current `/v1/data` output before any firmware changes; redact credentials from shared logs.

These are prerequisites for hardware validation, not a request to perform mass erases or alter option bytes. No programming command is automatically run by any tool added to this repository.

## Reproducing this investigation

From the repository root:

```sh
python3 tools/extract_firmware.py
python3 -m venv analysis/.venv
analysis/.venv/bin/python -m pip install -r tools/requirements-analysis.txt
analysis/.venv/bin/python tools/firmware_inspect.py wifi info
analysis/.venv/bin/python tools/firmware_inspect.py mcu info
analysis/.venv/bin/python tools/export_analysis.py
analysis/.venv/bin/python tools/firmware_inspect.py wifi dis 0x40206f64 --count 24
analysis/.venv/bin/python tools/firmware_inspect.py wifi refs 0x3ffef850
```

Extraction is standard-library-only. It verifies the known APK hash and ZIP CRCs, refuses a changed working copy or changed extracted assets, and checks both firmware hashes. Disassembly requires Capstone `6.0.0a11`; stable 5.x lacks the Xtensa target. The local analysis environment was populated from the earlier investigation's already-installed Capstone package, and successfully exercised for ARM and Xtensa.

The focused disassembler retains raw bytes, literal values, and recomputed absolute Xtensa call/jump targets. It handles narrow zero-branch encodings rejected by this Capstone prerelease. It stops on unknown encodings/returns. Linear excerpts can still include padding or data between blocks; they are evidence to inspect, not a complete decompilation. Cross-reference hits likewise need surrounding-code validation.

Files to retain:

- `analysis/source-provenance.json`, `apk-inventory.json`: provenance and APK manifest.
- `analysis/extracted/assets/www/fw/`: exact extracted stock images.
- `analysis/extracted/assets/www/`: readable Cordova application and its assets.
- `analysis/wifi-image-map.json`, `mcu-image-map.json`, `mcu-command-map.json`: structured binary facts.
- `analysis/disassembly/`: ten focused, regenerable listings.
- `analysis/reference/esp8266-arduino-2.4.0/`: exact upstream source references with URLs and SHA-256 in `sources.json`.
- `analysis/prior-analysis/`: preserved prior scripts and provenance, explicitly not independent evidence.

## Primary upstream references

The exact Arduino tag was downloaded for function matching; current/master code differs and should not be substituted silently.

- [ESP8266 Arduino 2.4.0 Wi-Fi AP implementation](https://github.com/esp8266/Arduino/blob/2.4.0/libraries/ESP8266WiFi/src/ESP8266WiFiAP.cpp): AP setup/disconnect behavior.
- [ESP8266 Arduino 2.4.0 generic Wi-Fi implementation](https://github.com/esp8266/Arduino/blob/2.4.0/libraries/ESP8266WiFi/src/ESP8266WiFiGeneric.cpp): persistence default and mode-bit helpers.
- [ESP8266 Arduino 2.4.0 station implementation](https://github.com/esp8266/Arduino/blob/2.4.0/libraries/ESP8266WiFi/src/ESP8266WiFiSTA.cpp): reference for eventual station connection/provisioning code; its full Arduino wrapper is not thereby proven present in the stock binary.

## Status at the end of the initial extraction pass

- Extracted and hash-verified both firmware images; verified the source APK remains identical.
- Identified the network lifecycle and the station-loss issue in AP shutdown.
- Reconstructed controller command IDs plus request/response lengths, and checked selected recovery paths.
- Confirmed the Sunday clock bug and identified the saved-credential size mismatch.
- Added reproducible, read-only inspection tools and detailed evidence listings.
- No device operations, custom firmware build, flash, or MQTT changes performed. Remaining work is completing the normal UART framing/lifecycle and preparing a bounded station-mode patch or replacement, then validating on backed-up hardware.

Final offline verification: all APK ZIP-entry CRCs passed; source and copy SHA-256 matched the recorded value; both ESP image checksums passed; flipping one segment bit in memory caused each corresponding image checksum to fail; segment address/offset round trips passed at both ends of every mapped segment; repeated exports were byte-for-byte deterministic; the decoded controller dispatcher contained 14 supported request commands; all three analysis scripts passed Python syntax compilation. The `original/` directory still contains only the supplied APK. These checks validate extraction and analysis mechanics, not firmware execution on hardware.

## Replacement firmware pass — 2026-10-20

The user authorized reconstructing the GARO protocol and writing replacement ESP8266 firmware for local home automation. Source is under `firmware/`, with [build/provisioning/API documentation](../firmware/README.md) and a separate [protocol specification](GARO-PROTOCOL.md).

Implemented a portable C++ protocol library with bounded streaming decode, CRC16, settings codecs, state decode, confirmation/heartbeat, periodic polling, request matching, and asynchronous write results. The application uses station-only Wi-Fi, DHCP/reconnect, and an authenticated local HTTP API. Network association does not block controller communication. MQTT remains deferred.

The bridge intentionally omits stock Android UI, AP configuration, full-state overwrite, and all firmware-transfer/ROM-programming paths. The ordinary STM32 heater firmware is retained. Its original modes, curves, local timers, and working-window logic still determine heating behavior; there is no fabricated relay telemetry or new direct on/off command.

### New binary evidence

- ESP transmitter at `0x40208140`: serializes `00`, length, command, payload, CRC low/high.
- ESP receiver at `0x4020836c`: validates command-specific sizes and CRC.
- ESP CRC at `0x40210ab8`: skips the length byte; starts at zero; covers command plus payload.
- ESP state serializer at `0x4020acdc`: resolves mode order and 91-byte response/version offsets.
- The stock scheduler's idle path sends **0x01 periodically**, which explains how to preserve confirmation/liveness without depending on HTTP traffic.
- GPIO write routine `0x401069ac` accesses GPIO set/clear registers; GPIO configuration routine `0x402013f8` matches pinMode-like behavior. Stock idle levels are GPIO5 low and GPIO4 high. The replacement preserves these levels; physical net mapping remains unverified.
- The special 0x06 tables differ by direction/path: MCU ordinary request/response lengths are 1/1; ESP tables say 0/8. The replacement does not send this poorly understood command. It recognizes other known events without starting an AP or erasing router credentials.

Focused normal-protocol listings were saved under `analysis/disassembly/wifi-normal-*.txt` and `wifi-state-json.txt`. `tools/firmware_flow.py` traverses bounded direct control-flow branches to avoid interpreting unreachable alignment bytes as instructions; it does not automatically recover indirect jump tables.

### Implemented network and command boundaries

Station credentials and API token are separate compile-time strings in an ignored `firmware/include/local_config.h`. They are length checked and never reuse stock `/config`. With no local configuration present, the image remains unconfigured, starts no AP, and has no configured router to join. UART servicing still runs.

The first HTTP implementation used the Arduino WebServer library. Inspection showed blocking header/body reads could interfere with liveness under a slow client, so the final implementation uses an incremental bounded request parser. It processes at most 256 input bytes per main-loop pass, allows 2048 header bytes/1024 body bytes, sets a 2-second total request deadline, and uses a bounded client write timeout. Requests with duplicate lengths/auth or unsupported transfer encoding are rejected. This change also removes the WebServer dependency.

Control writes return a queued operation ID and require a matching CRC-valid controller response before acceptance is reported. The bridge never retries an uncertain settings write automatically. A stale snapshot returns 503 rather than masquerading as current state. Both the byte protocol and API retain the distinction between configured state and measured physical state.

### Validation results

1. Native protocol and HTTP parser tests passed under address/undefined-behavior sanitizers, covering framing, fragmentation, corruption, signed curves, calendar/Sunday handling, request matching, rejected/lost acknowledgments, recovery events, stale state, millis wraparound, input limits, and deterministic arbitrary-byte input.
2. An independent test executed the **original STM32 frame builder and CRC machine code** in Unicorn, stubbing only its final UART send routine. **136 frames matched the new C++ encoder byte for byte**, including payload lengths 0–128. Result and vectors: `analysis/stock-protocol-verification.json`. This is stronger than a new encoder/decoder round-trip test, but it does not emulate the entire heater.
3. The ESP8266 build succeeded with pinned platform 4.2.1 / Arduino core 3.1.2 / ArduinoJson 6.21.5. The unconfigured development build used 36,872 bytes of statically allocated RAM and 286,539 bytes of linked flash, and exported a 290,688-byte image. Configured builds may differ; the generated artifact manifest is authoritative for each build.
4. The build tool verifies loader XOR, application XOR with Arduino 3 metadata normalized, and the full Arduino CRC32. Arduino 3's generator writes a size/CRC32 pair into the app after calculating XOR, so the old stock-image XOR checker alone would misleadingly flag the finished new app. No checksum bytes were patched to hide this difference.
5. The original APK and both extracted stock firmware hashes remain unchanged. No device, external UART programmer, live API, STM32 option bytes, or hardware flash was touched.

Exported artifacts are under ignored `build/`, with an integrity/source manifest; rebuild using `analysis/.venv/bin/python tools/build_firmware.py`. The default image has no credentials/token and is marked hardware-untested. The Python helper `tools/idvn.py` and an optional Home Assistant REST configuration demonstrate automation without adding MQTT prematurely.

### Still required before deployment

Read the actual board/flash identities and make full backups; verify isolation and GPIO/serial wiring; capture a stock boot and settings transaction; run the bridge through boot, confirmation, router absence/reboot, local timer behavior, and several recovery intervals on the bench. Validate actual recovery/rollback behavior and ensure the controller does not overwrite the replacement image. First hardware tests should observe the controller/display and a suitable test load rather than assume remote settings imply relay activation. These are physical validation gaps, not tasks an offline build can establish.
