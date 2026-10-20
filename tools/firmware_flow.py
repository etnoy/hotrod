#!/usr/bin/env python3
"""Bounded reachable instruction listing; does not follow calls or jump tables."""
import argparse
import re
from firmware_inspect import Image


def flow(image, start, end, limit=2000):
    pending, seen = [start], {}
    while pending and len(seen) < limit:
        address = pending.pop()
        if address in seen or not start <= address < end:
            continue
        line = next(image.disassemble(address, 1))
        seen[address] = line
        fields = line.split(None, 3)
        if len(fields) < 3 or '<undecoded' in line:
            continue
        raw, mnemonic = fields[1:3]
        operands = fields[3] if len(fields) > 3 else ''
        size = len(raw) // 2
        terminal = (mnemonic in ('ret', 'ret.n', 'jx', 'ill') or
                    mnemonic == 'bx' or mnemonic == 'pop' and 'pc' in operands or
                    mnemonic == 'add' and operands.startswith('pc,'))
        if terminal:
            continue
        branch = (mnemonic.startswith('b') and mnemonic not in ('bl', 'blx', 'break', 'break.n') or mnemonic == 'j')
        if branch:
            target = None
            if image.chip == 'mcu':
                match = re.match(r'0x([0-9a-f]+)', operands)
                if match:
                    target = int(match.group(1), 16)
            else:
                match = re.search(r'absolute target (0x[0-9a-f]+)', operands)
                if match:
                    target = int(match.group(1), 0)
                else:
                    match = re.search(r'\. ([+-])(0x[0-9a-f]+|[0-9]+)', operands)
                    if match:
                        target = address + int(match.group(2), 0) * (1 if match.group(1) == '+' else -1)
                    else:
                        match = re.search(r', (0x[0-9a-f]+) ; decoded', operands)
                        if match:
                            target = int(match.group(1), 0)
            if target is not None:
                pending.append(target)
            if mnemonic in ('b', 'j'):
                continue
        pending.append(address + size)
    return '\n'.join(seen[address] for address in sorted(seen))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('chip', choices=['wifi', 'mcu'])
    parser.add_argument('start', type=lambda value: int(value, 0))
    parser.add_argument('end', type=lambda value: int(value, 0))
    args = parser.parse_args()
    print(flow(Image(args.chip), args.start, args.end))
