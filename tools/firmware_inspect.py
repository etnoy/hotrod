#!/usr/bin/env python3
"""Read-only controller image inspection and focused disassembly. No device I/O."""
import argparse
import binascii
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "analysis/extracted/assets/www/fw"


def esp_image(data, start):
    magic, count, mode, size_freq, entry = struct.unpack_from("<BBBBI", data, start)
    if magic != 0xE9:
        raise ValueError(f"Bad ESP image magic at {start:#x}")
    pos, checksum, segments = start + 8, 0xEF, []
    for _ in range(count):
        addr, length = struct.unpack_from("<II", data, pos)
        pos += 8
        if pos + length > len(data):
            raise ValueError("Truncated segment")
        segments.append({"address": addr, "size": length, "offset": pos})
        for byte in data[pos:pos + length]:
            checksum ^= byte
        pos += length
    check_offset = pos | 15
    stored = data[check_offset]
    return {"offset": start, "entry": entry, "flash_mode": mode,
            "flash_size_frequency": size_freq, "segments": segments,
            "checksum_offset": check_offset, "checksum": checksum,
            "stored_checksum": stored, "checksum_valid": checksum == stored}


class Image:
    def __init__(self, chip):
        self.chip = chip
        self.data = (FW / ("wifi.bin" if chip == "wifi" else "mcu.bin")).read_bytes()
        self.segments = (esp_image(self.data, 0x1000)["segments"] if chip == "wifi"
                         else [{"address": 0x08000000, "size": len(self.data), "offset": 0}])

    def offset(self, addr):
        for seg in self.segments:
            if seg["address"] <= addr < seg["address"] + seg["size"]:
                return seg["offset"] + addr - seg["address"]
        raise ValueError(f"Unmapped address {addr:#x}")

    def address(self, offset):
        for seg in self.segments:
            if seg["offset"] <= offset < seg["offset"] + seg["size"]:
                return seg["address"] + offset - seg["offset"]
        raise ValueError(f"Unmapped offset {offset:#x}")

    def word(self, addr):
        return struct.unpack_from("<I", self.data, self.offset(addr))[0]

    def string(self, addr):
        try:
            off = self.offset(addr)
        except ValueError:
            return None
        end = self.data.find(b"\0", off, off + 200)
        if end <= off:
            return None
        value = self.data[off:end]
        return value.decode("ascii") if all(32 <= b < 127 or b in (9, 10, 13) for b in value) else None

    def literals(self, value):
        result = []
        for seg in self.segments:
            for off in range(seg["offset"], seg["offset"] + seg["size"] - 3, 4):
                if struct.unpack_from("<I", self.data, off)[0] == value:
                    result.append(self.address(off))
        return result

    def refs(self, literal):
        result = []
        for seg in self.segments:
            if self.chip == "wifi" and seg["address"] < 0x40000000:
                continue
            step = 1 if self.chip == "wifi" else 2
            for off in range(seg["offset"], seg["offset"] + seg["size"] - 2, step):
                addr = self.address(off)
                b = self.data[off:off+3]
                if self.chip == "wifi":
                    if b[0] & 15 != 1:
                        continue
                    imm = b[1] | b[2] << 8
                    target = ((addr + 3) & ~3) + ((imm - 0x10000) << 2)
                else:
                    hw = b[0] | b[1] << 8
                    if hw & 0xF800 != 0x4800:
                        continue
                    target = ((addr + 4) & ~3) + ((hw & 255) << 2)
                if target == literal:
                    result.append(addr)
        return result

    def disassemble(self, start, count):
        import capstone
        md = (capstone.Cs(capstone.CS_ARCH_XTENSA, 0) if self.chip == "wifi" else
              capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB | capstone.CS_MODE_MCLASS))
        addr = start
        for _ in range(count):
            off = self.offset(addr)
            raw = self.data[off:off+4]
            ins = next(md.disasm(raw, addr, count=1), None)
            # capstone 6.0.0a11 rejects some valid narrow Xtensa zero branches.
            if self.chip == "wifi" and raw[0] & 15 == 12 and raw[0] & 0x80:
                imm = (((raw[0] >> 4) & 3) << 4) | (raw[1] >> 4)
                name = "bnez.n" if raw[0] & 0x40 else "beqz.n"
                yield f"{addr:#010x}  {raw[:2].hex():8} {name:9} a{raw[1] & 15}, {addr + 4 + imm:#x} ; decoded from bytes"
                addr += 2
                continue
            if ins is None:
                yield f"{addr:#010x}  {raw.hex():8} <undecoded; stopping>"
                return
            annotation = ""
            if self.chip == "wifi":
                if ins.mnemonic == "l32r":
                    target = ((addr + 3) & ~3) + (((raw[1] | raw[2] << 8) - 0x10000) << 2)
                    try:
                        value = self.word(target)
                        annotation = f" ; [{target:#x}]={value:#x}"
                        text = self.string(value)
                        if text:
                            annotation += " " + repr(text)
                    except ValueError:
                        annotation = f" ; literal {target:#x} unmapped"
                if ins.mnemonic in ("call0", "call4", "call8", "call12", "j"):
                    imm = (raw[0] | raw[1] << 8 | raw[2] << 16) >> 6
                    if imm & 0x20000:
                        imm -= 0x40000
                    target = ((addr & ~3) + 4 + imm * 4 if ins.mnemonic != "j" else addr + 4 + imm)
                    annotation += f" ; absolute target {target:#x}"
            else:
                match = re.search(r"\[pc, #(0x[0-9a-f]+|\d+)\]", ins.op_str)
                if ins.mnemonic.startswith("ldr") and match:
                    target = ((addr + 4) & ~3) + int(match.group(1), 0)
                    try:
                        annotation = f" ; [{target:#x}]={self.word(target):#x}"
                    except ValueError:
                        pass
            yield f"{addr:#010x}  {bytes(ins.bytes).hex():8} {ins.mnemonic:9} {ins.op_str}{annotation}"
            addr += ins.size
            if (ins.mnemonic in ("ret", "ret.n") or
                ins.mnemonic == "bx" and ins.op_str == "lr" or
                ins.mnemonic == "pop" and "pc" in ins.op_str):
                return


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("chip", choices=["wifi", "mcu"])
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("info")
    dis = sub.add_parser("dis")
    dis.add_argument("address", type=lambda x: int(x, 0))
    dis.add_argument("--count", type=int, default=80)
    refs = sub.add_parser("refs")
    refs.add_argument("value", type=lambda x: int(x, 0))
    args = parser.parse_args()
    image = Image(args.chip)
    if args.command == "info":
        info = {"size": len(image.data), "sha256": hashlib.sha256(image.data).hexdigest(),
                "crc16_xmodem": binascii.crc_hqx(image.data, 0)}
        if args.chip == "wifi":
            info["images"] = [esp_image(image.data, off) for off in [0, 0x1000]]
        else:
            info["initial_sp"], info["reset_vector"] = struct.unpack_from("<II", image.data)
        print(json.dumps(info, indent=2))
    elif args.command == "dis":
        print("\n".join(image.disassemble(args.address, args.count)))
    else:
        for literal in image.literals(args.value):
            print(f"literal {literal:#x}: " + ", ".join(hex(ref) for ref in image.refs(literal)))


if __name__ == "__main__":
    main()
