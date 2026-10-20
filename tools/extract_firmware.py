#!/usr/bin/env python3
"""Verify the source APK, copy it, and extract web assets outside original/."""
import hashlib
import json
import re
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "original/Garo+IDVN_1.0.3_APKPure.apk"
EXPECTED = "c67728101c2272043de95f2a1a1fbc46293befea68b285224c7bea3c9c24877a"
FIRMWARE = {
    "wifi.bin": "2ae59d60c814fcb47f688c26ee70c7fb762b3ca5a40584115f5ffed0687caddb",
    "mcu.bin": "ee0048cfdefe9895c5824e6b5387a0ae7ca05d9076fc2c66e70502b2c2b17ba0",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    source_hash = digest(SOURCE)
    if source_hash != EXPECTED:
        raise ValueError("Source APK differs from the documented input; refusing extraction")
    out = ROOT / "analysis"
    out.mkdir(exist_ok=True)
    copy = out / "Garo-IDVN_1.0.3.apk"
    if copy.exists():
        if digest(copy) != source_hash:
            raise ValueError("Existing working copy differs; refusing to overwrite it")
    else:
        shutil.copyfile(SOURCE, copy)
    if digest(copy) != source_hash:
        raise ValueError("Working copy verification failed")
    extracted = (out / "extracted").resolve()
    with zipfile.ZipFile(copy) as archive:
        bad = archive.testzip()
        if bad:
            raise ValueError(f"ZIP CRC check failed: {bad}")
        inventory = []
        for entry in archive.infolist():
            inventory.append({"path": entry.filename, "size": entry.file_size,
                              "compressed_size": entry.compress_size, "crc32": f"{entry.CRC:08x}"})
            if not entry.filename.startswith("assets/www/") or entry.is_dir():
                continue
            dest = (extracted / entry.filename).resolve()
            if extracted not in dest.parents:
                raise ValueError("Archive entry escapes extraction directory")
            content = archive.read(entry)
            if dest.exists() and dest.read_bytes() != content:
                raise ValueError(f"Modified extracted file exists: {dest}; refusing overwrite")
            dest.parent.mkdir(parents=True, exist_ok=True)
            if not dest.exists():
                dest.write_bytes(content)
    (out / "apk-inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
    (out / "source-provenance.json").write_text(json.dumps({
        "source": str(SOURCE.relative_to(ROOT)), "working_copy": str(copy.relative_to(ROOT)),
        "size": copy.stat().st_size, "sha256": source_hash}, indent=2) + "\n")
    for name, expected in FIRMWARE.items():
        path = extracted / "assets/www/fw" / name
        if digest(path) != expected:
            raise ValueError(f"Unexpected firmware content: {name}")
        data = path.read_bytes()
        strings = [f"{match.start():08x}  {match.group().decode('ascii')}"
                   for match in re.finditer(rb"[\x20-\x7e]{4,}", data)]
        (out / (name + ".strings.txt")).write_text("\n".join(strings) + "\n")
        print(f"{name}: {len(data)} bytes, SHA-256 {expected}")
    if digest(SOURCE) != source_hash:
        raise ValueError("Source hash changed during extraction")
    print("Source and working-copy hashes match; all ZIP entries passed CRC validation.")


if __name__ == "__main__":
    main()
