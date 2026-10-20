# GARO IDVN bridge

Garo IDVN is a **motorvärmare** or an electric engine preheater used to warm a parked vehicle before driving in cold weather. The vehicle connects to a mains outlet through its heater cable; a compatible cabin heater can warm the interior as well. Drivers typically choose a departure time so heating takes place before they leave.

The **GARO IDVN** is the outlet controller supplying the vehicle's heater equipment. It has two separately controlled outlets and supports departure, maintenance-heating, and timer modes. Departure heating uses an outdoor-temperature curve to determine when power should start. See [GARO's product overview](https://www.garo.se/storage/450C4FF9CC87E13B3BE03FCC457D65DF1A9F667B94AB51EA881D33F796482D54/1bb66bdfcdf44b60913e4e75939b37fc/pdf/media/30970599720e4aacab5474d57646fdde/Utvalt.pdf) and [IDVN instructions](https://www.garo.se/storage/EB9CB181E178D6216F6DEEDD89832E1E4AB5AB3A4325E20995DAF007E2F92FF9/d28193d8d89746b0b5edae8f0c835d59/pdf/media/a18c98d5f0944b7184dd4f50a1a73423/Monteringsanvisning%20%28SE-FI%29%20IDVN%20380229-1.pdf) for the manufacturer's operating instructions.

This project replaces the IDVN's **ESP8266 Wi-Fi firmware** so it can join an existing home network and expose the controller's settings to local home automation. The original STM32 firmware continues to manage the heater. MQTT is planned for a later stage; the first version uses a small authenticated HTTP API.

This is an unofficial project, with no GARO affiliation or endorsement. **Development status: compiled and tested offline, but not yet tested on an appliance.** Board wiring, programming access, and recovery still require physical validation.

## What it can do

- Join Wi-Fi in station/client mode, with DHCP and reconnect.
- Maintain the controller's startup confirmation and keep-alive independently of Wi-Fi availability.
- Read configured modes, heating curves, working windows, clock, and versions.
- Change those settings through a local API, with explicit controller acknowledgment results.
- Integrate with automation using the [Python client](tools/idvn.py) or [Home Assistant REST example](examples/home-assistant.yaml).

The current controller protocol does not expose measured relay state or outdoor temperature, a direct arbitrary on/off command, or remote editing of the local departure-time slots. The controller's existing timers and heating rules still apply. See the [firmware documentation](firmware/README.md) for the full API and limitations.

## Build

The commands below assume macOS or Linux and run from the repository root. They require Python 3 and internet access for the first dependency download. Native tests also require clang++ with address/undefined-behavior sanitizer support; set `CXX` if a compatible compiler has another name. Windows users need to adapt virtual-environment paths and serial-port names.

The replacement firmware builds without the original APK or stock firmware.

1. Create the build environment:

   ```sh
   python3 -m venv analysis/.venv
   analysis/.venv/bin/python -m pip install platformio==6.1.18
   ```

2. Create a local configuration without overwriting an existing one:

   ```sh
   cp -n firmware/include/local_config.h.example firmware/include/local_config.h
   ```

   Edit `firmware/include/local_config.h` and set `GARO_WIFI_SSID`, `GARO_WIFI_PASSWORD`, and a unique `GARO_API_TOKEN` of 24–128 bytes. Use a compatible 2.4 GHz Wi-Fi network. The configuration file and build outputs are ignored by Git. Credentials are embedded in the resulting binary, so keep configured images private.

   GPIO4/GPIO5 idle levels are board settings: verify their wiring before changing them. With no configured SSID/token, the image is an unconfigured development build. It does not offer a fallback AP or network credential editor.

3. Build and verify the image, then run the offline tests:

   ```sh
   analysis/.venv/bin/python tools/build_firmware.py
   python3 tools/test_firmware.py
   ```

   The build pins ESP8266 platform 4.2.1, Arduino core 3.1.2, and ArduinoJson 6.21.5. It checks image integrity and exports:

   - `build/garo-idvn-bridge.bin`: combined ESP8266 bootloader/application image.
   - `build/garo-idvn-bridge.json`: image hash, source manifest, dependency versions, and integrity results.
   - `firmware/.pio/build/garo_esp8266/firmware.elf`: executable with symbols for debugging.

The build tool never flashes hardware. Native tests cover framing, controller lifecycle, settings validation, and bounded HTTP input. Separately, 136 generated frames matched the original STM32 encoder byte for byte; this verifies the frame format, not operation on the physical heater.

## Flash the ESP8266

**The following is a manual bench procedure, not a verified IDVN pad/pin guide.** This project has no confirmed PCB photograph or programming-header map. Confirm the exact board, flash capacity, power arrangement, and recovery access before using these commands. The configured build currently assumes **4 MiB flash, DIO, 40 MHz**.

### Prepare the hardware

Disconnect the unit from mains. Its logic may be mains-referenced: do not connect a grounded USB programmer while the appliance is mains-powered. Establish a safe bench-power arrangement and a verified logic supply point before connecting anything. Use a suitable 3.3 V supply and **3.3 V UART logic**, never 5 V signals.

Identify the ESP8266's GND, 3V3, TX/GPIO1, RX/GPIO3, GPIO0, EN, and reset. Connect adapter TX to ESP RX, adapter RX to ESP TX, and the appropriate common logic ground. The STM32 shares the serial connection: hold it in reset using a verified reset net, or isolate its UART so it cannot contend with the programmer. Do not guess which test pad or ESP GPIO reaches that net.

Enter the ESP8266 ROM downloader by holding GPIO0 low while resetting the ESP. EN must be high; retain the correct board straps for GPIO2 high and GPIO15 low. The commands below assume manual boot-mode entry and leave the ESP in the downloader after each command. See [Espressif's boot-mode guide](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html).

### Identify and back up

The build installs **esptool 3.0** inside its local toolchain. These examples use that version's underscore command names; newer esptool versions use different spelling. Replace the example port with your adapter's actual port (`/dev/ttyUSB0` is a common Linux example).

```sh
IDVN_PORT=/dev/cu.usbserial-XXXX
IDVN_ESPTOOL=analysis/platformio/packages/tool-esptoolpy/esptool.py

analysis/.venv/bin/python "$IDVN_ESPTOOL" \
  --chip esp8266 --port "$IDVN_PORT" --baud 115200 \
  --before no_reset --after no_reset flash_id
```

Continue with the **4 MiB example below only if the detected flash capacity and board match the build**. Otherwise stop and qualify the correct build layout first. Read the full flash twice before writing anything; the APK's update image is not a full-device backup.

```sh
mkdir -p analysis/backups

analysis/.venv/bin/python "$IDVN_ESPTOOL" \
  --chip esp8266 --port "$IDVN_PORT" --baud 115200 \
  --before no_reset --after no_reset \
  read_flash 0x000000 0x400000 analysis/backups/esp8266-full-A.bin

analysis/.venv/bin/python "$IDVN_ESPTOOL" \
  --chip esp8266 --port "$IDVN_PORT" --baud 115200 \
  --before no_reset --after no_reset \
  read_flash 0x000000 0x400000 analysis/backups/esp8266-full-B.bin

cmp analysis/backups/esp8266-full-A.bin analysis/backups/esp8266-full-B.bin
```

Both reads must complete and `cmp` must report no difference. Re-enter download mode if a new command cannot connect. Retain a separate private copy of the matching backup: it includes device settings, calibration, and potentially credentials. `analysis/` is ignored by Git, and neither backups nor vendor firmware should be attached to public releases.

### Write and verify the replacement

After the backup, confirm that your SSID/token are configured and the hardware checks above are complete. Write the **combined image at address 0x000000**, not 0x1000. Keep the build's header parameters unchanged:

```sh
analysis/.venv/bin/python "$IDVN_ESPTOOL" \
  --chip esp8266 --port "$IDVN_PORT" --baud 115200 \
  --before no_reset --after no_reset \
  write_flash --flash_mode keep --flash_freq keep --flash_size keep \
  0x000000 build/garo-idvn-bridge.bin

analysis/.venv/bin/python "$IDVN_ESPTOOL" \
  --chip esp8266 --port "$IDVN_PORT" --baud 115200 \
  --before no_reset --after no_reset \
  verify_flash 0x000000 build/garo-idvn-bridge.bin
```

This procedure does not request a whole-chip erase. Do not add `--erase-all` or run `erase_flash`; preserve the remaining flash contents and your full backup. Do not use the stock app's firmware-update endpoints for this development image. The commands target only the ESP8266; this project does not program the STM32 or change its option bytes.

Power down the bench setup, release GPIO0 to its normal high state, and restore the normal board wiring. Disconnect the programmer's TX/RX before releasing the STM32 reset to avoid contention on the shared UART. Boot under the validated power arrangement. The firmware should join the configured network; find its DHCP address in the router.

### Check the first boot

Set the bridge's LAN address and the same API token configured in the image, then use read-only checks first:

```sh
export GARO_URL=http://192.168.1.80
export GARO_API_TOKEN='replace-with-your-configured-token'

python3 tools/idvn.py status
python3 tools/idvn.py data
```

Check controller confirmation, fresh data, and UART errors. Observe the unit for several 90-second recovery intervals, test loss/restoration of Wi-Fi, and confirm that the controller does not restore stock ESP firmware. Only then test individual settings changes against the unit's display and a suitable test load. Hardware validation is still required before unattended heater operation.

The DHCP hostname defaults to `garo-idvn`; mDNS discovery is not implemented. The stock Android app's fixed address is not used by this bridge. The API reference, clock-sync command, and automation examples are in [firmware/README.md](firmware/README.md).

### Restore the full backup

If recovery is necessary, return to the same safe bench/download arrangement with the STM32 isolated from the programmer. For the confirmed 4 MiB device, restore **its own** complete backup and verify it:

```sh
analysis/.venv/bin/python "$IDVN_ESPTOOL" \
  --chip esp8266 --port "$IDVN_PORT" --baud 115200 \
  --before no_reset --after no_reset \
  write_flash --flash_mode keep --flash_freq keep --flash_size keep \
  0x000000 analysis/backups/esp8266-full-A.bin

analysis/.venv/bin/python "$IDVN_ESPTOOL" \
  --chip esp8266 --port "$IDVN_PORT" --baud 115200 \
  --before no_reset --after no_reset \
  verify_flash 0x000000 analysis/backups/esp8266-full-A.bin
```

Restore normal boot straps and wiring as above. The controller's apparent automatic rollback is not a substitute for a verified physical recovery path. Flash-command behavior is documented in [Espressif's esptool guide](https://docs.espressif.com/projects/esptool/en/latest/esp8266/esptool/basic-commands.html); use the bundled tool's `--help` for the exact v3 syntax shown here.

## Research and repository contents

- [Investigation notebook](docs/FIRMWARE-NOTES.md): evidence, addresses, hypotheses, and progress.
- [Protocol specification](docs/GARO-PROTOCOL.md): frame format, payload layouts, and remaining uncertainties.
- [Firmware documentation](firmware/README.md): API, configuration, architecture, and validation.
- [Home Assistant example](examples/home-assistant.yaml): optional local REST integration.

Vendor APKs, extracted firmware/assets, downloaded vendor sources, toolchains, disassembly, device backups, credentials, and compiled images are excluded by `.gitignore`. They are not included in the public source repository.

For local research, put a lawfully obtained APK at the path documented in `tools/extract_firmware.py`, under `original/`. **Never modify anything in `original/`.** Run `python3 tools/extract_firmware.py` to verify and extract the expected input into `analysis/`. Research tools that compare against the stock binary require those local inputs; building the replacement does not.

## License

The project-authored source and documentation are available under the [MIT License](LICENSE). Copyright 2026 Jonathan Jogenfors.

Third-party dependencies retain their own licenses; this project's MIT license does not relicense the Arduino core, Espressif SDK, ArduinoJson, or GARO's software/assets. Review the applicable dependency licenses and notices before distributing compiled firmware.
