#!/usr/bin/env python3
"""Compare the new encoder with the unmodified STM32 machine-code encoder.

Only the UART driver is stubbed. The actual stock frame builder and CRC code run
in Unicorn. This does not emulate or connect to the appliance's hardware.
"""
import hashlib
import json
from pathlib import Path
import random
import subprocess
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP, UC_ARM_REG_LR

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'analysis/extracted/assets/www/fw/mcu.bin'
EXPECTED='ee0048cfdefe9895c5824e6b5387a0ae7ca05d9076fc2c66e70502b2c2b17ba0'


def stock_frame(image,command,payload):
    machine=Uc(UC_ARCH_ARM,UC_MODE_THUMB)
    machine.mem_map(0x08000000,0x10000)
    machine.mem_write(0x08000000,image)
    machine.mem_map(0x20000000,0x10000)
    machine.reg_write(UC_ARM_REG_SP,0x2000f000)
    machine.reg_write(UC_ARM_REG_LR,0x0800fff1)
    buffer=0x20001000
    machine.mem_write(buffer,bytes([0,len(payload),command])+payload+b'\x00\x00')
    machine.reg_write(UC_ARM_REG_R0,buffer)
    result=[]

    def uart(machine,address,size,context):
        if address!=0x080098c6:
            return
        pointer=machine.reg_read(UC_ARM_REG_R0)
        length=machine.reg_read(UC_ARM_REG_R1)
        result.append(bytes(machine.mem_read(pointer,length)))
        machine.emu_stop()

    machine.hook_add(UC_HOOK_CODE,uart,begin=0x080098c6,end=0x080098c6)
    machine.emu_start(0x08006125,0x0800fff0,count=100000)
    if len(result)!=1:
        raise RuntimeError('Stock encoder did not reach the UART stub')
    return result[0]


def main():
    image=SOURCE.read_bytes()
    if hashlib.sha256(image).hexdigest()!=EXPECTED:
        raise ValueError('Unexpected stock firmware input')
    build=ROOT/'build'; build.mkdir(exist_ok=True)
    subprocess.run(['clang++','-std=c++11','-Wall','-Wextra','-Werror',
        '-I'+str(ROOT/'firmware/lib/garo'),str(ROOT/'tests/frame_encoder.cpp'),
        str(ROOT/'firmware/lib/garo/GaroProtocol.cpp'),'-o',str(build/'frame_encoder')],check=True)
    cases=[(1,b''),(0x11,b''),(0x13,b''),(0x28,b''),
           (0x21,bytes([2,1,3,2])),(0x15,bytes([26,10,25,7,6,30,0]))]
    rng=random.Random(0x1d7)
    for length in range(129):
        cases.append((0x28,bytes(rng.randrange(256) for _ in range(length))))
    cases.extend((0x25,bytes([0]*32)) for _ in range(1))
    encoded=subprocess.run([str(build/'frame_encoder')],text=True,capture_output=True,check=True,
        input='\n'.join(bytes([command]).hex()+payload.hex() for command,payload in cases)+'\n')
    actual=encoded.stdout.splitlines()
    if len(actual)!=len(cases):
        raise AssertionError('Unexpected native encoder output count')
    vectors=[]
    for (command,payload),output in zip(cases,actual):
        expected=stock_frame(image,command,payload)
        if bytes.fromhex(output)!=expected:
            raise AssertionError(f'Frame mismatch for command {command:#x}, payload {payload.hex()}')
        vectors.append({'command':command,'payload_hex':payload.hex(),'wire_hex':expected.hex()})
    report={'stock_sha256':EXPECTED,'entry':'0x08006124','uart_stub':'0x080098c6',
            'comparisons':len(cases),'all_matched':True,'vectors':vectors}
    (ROOT/'analysis/stock-protocol-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'{len(cases)} frames matched the original STM32 encoder byte for byte (payload sizes 0–128).')


if __name__=='__main__':
    main()
