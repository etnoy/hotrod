# GARO IDVN controller protocol

Reconstructed 2026-10-20 from the exact stock images recorded in [FIRMWARE-NOTES.md](FIRMWARE-NOTES.md). This describes the normal settings link used by the replacement firmware. The bootloaders use different protocols. No hardware capture has yet confirmed electrical timing or rollback behavior.

## UART and frame format

Normal operation uses UART0 at **115200 baud, 8N1**, with GPIO1 TX and GPIO3 RX on the ESP8266. No console output can share this UART. The immutable ESP boot ROM can still print its boot banner at its own baud rate.

For a payload of N bytes, the wire frame is:

```text
00  (N+4)  CMD  PAYLOAD[N]  CRC_LOW  CRC_HIGH
```

- Total wire length: N+5 bytes.
- Length byte: N+4; includes itself, command, payload, and CRC, but excludes the start byte.
- CRC: CRC-16/XMODEM (polynomial 0x1021, initial zero, non-reflected, no final XOR).
- CRC input: command followed by payload; neither start nor length is included.
- CRC transmission order: low byte first.
- The normal link does not use the ESP ROM-loader's SLIP escaping. Zero bytes may appear inside payloads and CRCs.
- The implemented bounded decoder supports payloads up to 128 bytes. The bridge only transmits its explicit settings/lifecycle allowlist.

Empty-payload examples:

```text
Confirm/startup heartbeat: 00 04 01 21 10
Read configuration:       00 04 28 6a a5
Identify:                 00 04 13 52 22
Activity indication:      00 04 11 10 02
```

Evidence: ESP transmitter `0x40208140` explicitly writes a zero byte before the length-prefixed body; CRC routine `0x40210ab8` skips the length and covers command plus payload. ESP receiver `0x4020836c` checks zero, length, command-specific expected size, and CRC. MCU `0x080060ac` computes the same CRC over command and payload; `0x08006124` builds the frame and calls UART transmit at `0x080098c6`.

Regenerable focused listings are in `analysis/disassembly/wifi-normal-{tx,rx,crc,dispatch,scheduler}.txt`. Use `tools/firmware_flow.py` for bounded reachable listings; it deliberately does not follow indirect jump tables automatically.

## Commands used by the replacement

Command 0x01 has no request payload and no response payload. The MCU clears pending Wi-Fi recovery state and confirms startup. The stock scheduler uses this command periodically as well; it is not necessary to send 0x11 merely to satisfy the recovery deadline.

Command 0x28 has no request payload and returns the 91-byte snapshot described below.

Commands 0x13 (identify), 0x15 (clock), 0x21 (mode), 0x23 (working window), 0x25 (departure curve), and 0x27 (reduction curve) return one status byte: zero for acceptance, nonzero for a controller error. The bridge retains the raw error number instead of inventing meanings for all values. A CRC-valid matching reply is required before a submitted operation becomes `applied`.

These confirmations indicate settings acceptance, not measured electrical power or relay position. An acknowledgment lost after execution leaves the result uncertain; the replacement does not automatically resend a mutating command.

## Payload layouts

### Mode, command 0x21: 4 bytes

```text
left_mode, right_mode, timer_count, reduction_period
```

Mode values: 0 departure, 1 reduction, 2 timer. Timer count: 1–3. Period: 0=60 minutes, 1=30 minutes, 2=15 minutes. **Timer count precedes period on the wire**, even though the app's JSON field ordering can differ.

### Working window, command 0x23: 9 bytes

```text
days,
from_hours, from_minutes, to_hours, to_minutes,
from_month, from_day, to_month, to_day
```

The bridge validates time/date ranges and a days value 0–127. It does not reinterpret the controller's scheduling rules. In particular, do not use `days=0` as a presumed off command; the earlier reverse-engineering report identified a special all-days interpretation that still needs a dedicated operational test.

### Clock, command 0x15: 7 bytes

```text
year_00_to_99, month, day, weekday_1_to_7, hours, minutes, seconds
```

The bridge uses conventional hours 0–23, minutes/seconds 0–59, and calendar-valid dates in 2000–2099. Weekday is Monday=1 through Sunday=7. The stock Android app sends Sunday=0, which MCU validator `0x080087e4` rejects. The helper client fixes this using Python `isoweekday()`.

This is a local wall clock. There is no timezone byte. The bridge does not set a fabricated date at boot or assume UTC: home automation must send the appropriate local time and resynchronize after DST changes/power loss.

### Curves, commands 0x25/0x27: 32 bytes

Sixteen pairs of signed temperature byte followed by unsigned minutes byte. An unused pair has minutes 0xff; the bridge pads unused pairs with `ff ff`.

The bridge accepts 2–16 points with strictly increasing temperatures from -35 to 15 Celsius. Departure minutes: 0–210. Reduction minutes: 0–60, stored per 60 minutes; period scaling belongs to the controller/UI interpretation. Signed temperature -20 is wire byte `ec`.

### Read-all response, command 0x28: 91 bytes

```text
00..03  mode (left, right, timer_count, period)
04..12  working window
13..44  departure curve
45..76  reduction curve
77..83  clock
84..85  hardware version, little endian
86..87  controller firmware version, little endian
88      controller storage capacity exponent
89..90  controller storage free KiB, little endian
```

The offsets above are **decimal**. Mapping is corroborated by the ESP JSON serializer at `0x4020acdc`. It resolves the earlier version ambiguity: the stock MCU emits hardware version **104** (`0x0068`) and controller version **100** (`0x0064`); these are different fields. Wi-Fi version 100 is supplied separately by the stock ESP application.

The snapshot has no temperature measurement, relay measurement, or local departure-time slot data.

## Startup, liveness, and failure handling

The replacement initializes the stock idle pin levels (GPIO5 low, GPIO4 high), configures UART, and sends command 0x01 immediately. It waits for the matching zero-payload response and polls 0x28. It continues 0x01 every 10 seconds and 0x28 every 15 seconds. Success of a user write requests an additional snapshot refresh.

All UART work is independent of Wi-Fi association and HTTP requests. No router, DHCP failure, and no API clients should still leave those messages running. Polling and heartbeat have priority over queued configuration changes. HTTP input is read incrementally, with 256 bytes per main-loop pass, 2 KiB headers, 1 KiB bodies, and a 2-second total request deadline. Response writes use a 100 ms client timeout.

There is one in-flight UART transaction. Replies must have the matching command and expected payload size. There are no transaction sequence numbers in this frame format. The replacement uses a 700 ms transaction deadline and a 1-second quiet period after timeout to discard late acknowledgments before sending another command. Those timings are engineering choices, not measured stock timing guarantees. Extremely late replies for a later request with the same opcode remain indistinguishable on this protocol; inspect state after uncertain results.

The decoder resets a partial frame after an inter-byte gap over 100 ms, rejects invalid lengths/CRC, and resumes searching for the zero start byte. These host-tested bounds need confirmation against real captures.

Cached configuration becomes unavailable to the API after 45 seconds without a valid snapshot. A successful heartbeat alone cannot make an old snapshot current.

## Controller-originated events and unresolved details

The stock ESP receive table additionally includes 0x02 (one byte), 0x05 (zero), 0x06 (eight), and 0x40 (zero). In stock code 0x05 causes Wi-Fi factory-config handling; 0x40 requests an AP lifecycle action. The replacement records 0x05, invalidates cached state, and reconfirms startup. It does not erase router credentials or start an AP in response to those events.

There is a discrepancy around 0x06: the MCU ordinary handler tables accept one request byte and emit one response byte, while the ESP transmit/receive tables list zero/eight. This may indicate a separate event/state path or a stock mismatch. **The replacement never sends 0x06** and does not rely on it for liveness. The event payload's meaning is not reconstructed.

The 0x29 full-config write and 0x30–0x32 firmware-transfer commands are excluded from the replacement's outgoing allowlist and HTTP surface. ROM programming, automatic rollback image transfer, controller replacement, and firmware uploads are not implemented. The replacement retains normal keep-alive/confirmation, rather than disabling the controller's recovery system.

GPIO4/5 register-level behavior is matched to the stock functions: IRAM `0x401069ac` writes GPIO set/clear registers and is digitalWrite-like; `0x402013f8` configures pins and is pinMode-like. The net mapping to physical BOOT0/NRST and board revision still require inspection. The 4 MiB build layout follows the stock ESP header declaration, not a JEDEC measurement.

## Verification boundaries

`python3 tools/test_firmware.py` checks the portable framing/codec/lifecycle and bounded HTTP parser under address/undefined-behavior sanitizers, including malformed input, lost acknowledgments, no implicit mutating retries, and millisecond-counter wraparound.

`tools/verify_stock_protocol.py` executed the original STM32 encoder/CRC instructions in Unicorn, stubbing only the final UART transmit function, and compared 136 cases with the new C++ encoder. All frames matched byte for byte. Results and vectors are saved in `analysis/stock-protocol-verification.json`.

None of these tests prove physical isolation, pin wiring, UART baud tolerance, Wi-Fi stability, relay behavior, or recovery on the assembled heater. Those remain bench validation work before deployment.
