#!/usr/bin/env python3
"""Regenerate focused listings and structured facts from the extracted images."""
import json
import struct
from pathlib import Path
from firmware_inspect import Image, esp_image

ROOT = Path(__file__).resolve().parents[1]
REGIONS = {
    "wifi-startup": ("wifi", [(0x4020b3f0, 12), (0x40206eb8, 37), (0x40206f21, 27),
        (0x40206f64, 24), (0x40206f9c, 17), (0x40206fc2, 2),
        (0x40206fc8, 23), (0x40206ffc, 5), (0x40207008, 8)]),
    "wifi-mode-helpers": ("wifi", [(0x4020b7bc, 28), (0x4020b80c, 15),
        (0x4020b838, 10), (0x4020b470, 14), (0x4020b494, 65),
        (0x4020b538, 50), (0x4020b6e8, 36)]),
    "wifi-config": ("wifi", [(0x40207070, 23), (0x402070b8, 30),
        (0x40207110, 34), (0x40207158, 25), (0x40208300, 15),
        (0x40208325, 8), (0x4020a488, 29), (0x4020a4d0, 7), (0x4020a4e5, 48)]),
    "wifi-uart": ("wifi", [(0x402080fc, 21), (0x40207f9c, 40),
        (0x402073e4, 28), (0x40207df8, 40), (0x40207470, 18)]),
    "wifi-http-routes": ("wifi", [(0x40208dc8, 300), (0x40208784, 44)]),
    "mcu-commands": ("mcu", [(0x08005ca4, 20), (0x08005d30, 360)]),
    "mcu-clock-validator": ("mcu", [(0x080087e4, 33)]),
    "mcu-watchdog": ("mcu", [(0x0800615c, 125), (0x08006402, 26)]),
    "mcu-lengths": ("mcu", [(0x0800996c, 6), (0x080099aa, 20),
        (0x080099d4, 6), (0x08009a20, 10)]),
    "mcu-frame-checksum": ("mcu", [(0x080060ac, 58), (0x08006124, 24)]),
}


def main():
    output = ROOT / "analysis/disassembly"
    output.mkdir(parents=True, exist_ok=True)
    for name, (chip, ranges) in REGIONS.items():
        image = Image(chip)
        listing = "# Focused linear excerpts, not a complete control-flow reconstruction.\n"
        listing += "# Raw bytes and absolute targets retained; padding/tables can stop a listing.\n"
        for start, count in ranges:
            listing += f"\n# {chip}: {start:#010x}, limit {count} instructions\n"
            listing += "\n".join(image.disassemble(start, count)) + "\n"
        (output / (name + ".txt")).write_text(listing)
    for chip in ["wifi", "mcu"]:
        image = Image(chip)
        result = {"bytes": len(image.data), "segments": image.segments}
        if chip == "wifi":
            result["images"] = [esp_image(image.data, off) for off in [0, 0x1000]]
        (ROOT / "analysis" / (chip + "-image-map.json")).write_text(json.dumps(result, indent=2) + "\n")
    image = Image("mcu")
    commands = []
    for command in range(1, 0x33):
        handler = 0x08005ccc + struct.unpack_from("<H", image.data,
                    image.offset(0x08005ccc) + 2 * (command - 1))[0]
        if handler == 0x08005fac:
            continue
        lengths = []
        for base, table in [(0x0800997a, 0x08009978), (0x080099e2, 0x080099e0)]:
            address = base + image.data[image.offset(table) + command - 1]
            ins = struct.unpack_from("<H", image.data, image.offset(address))[0]
            if ins & 0xff00 != 0x2000:
                raise ValueError(f"Unexpected length helper at {address:#x}")
            lengths.append(ins & 255)
        commands.append({"command": command, "handler": handler,
                         "request_payload_bytes": lengths[0], "response_payload_bytes": lengths[1]})
    (ROOT / "analysis/mcu-command-map.json").write_text(json.dumps(commands, indent=2) + "\n")
    print(f"Generated {len(REGIONS)} focused listings, image maps, and {len(commands)} controller commands.")


if __name__ == "__main__":
    main()
