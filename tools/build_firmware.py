#!/usr/bin/env python3
"""Build ESP8266 firmware and export a verified local artifact. Never uploads."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from firmware_inspect import esp_image

ROOT=Path(__file__).resolve().parents[1]


def crc32_mpeg2(data):
    crc=0xffffffff
    for byte in data:
        crc^=byte<<24
        for _ in range(8):
            crc=((crc<<1)^0x04c11db7 if crc&0x80000000 else crc<<1)&0xffffffff
    return crc


def verify(data):
    loader=esp_image(data,0)
    if not loader['checksum_valid']:
        raise ValueError('Invalid bootloader XOR checksum')
    # Arduino 3.1.2's elf2bin writes CRC metadata into irom AFTER the XOR byte.
    # Validate the full CRC32 and the XOR over the original zero metadata slots.
    size,stored=struct.unpack_from('<II',data,0x1010)
    normalized=bytearray(data)
    normalized[0x1010:0x1018]=bytes(8)
    if size!=len(data) or crc32_mpeg2(normalized)!=stored:
        raise ValueError('Invalid Arduino image size/CRC32 metadata')
    application=esp_image(normalized,0x1000)
    if not application['checksum_valid']:
        raise ValueError('Invalid application XOR checksum after metadata normalization')
    return {'loader_xor_valid':True,'application_pre_metadata_xor_valid':True,
            'arduino_crc32_valid':True,'arduino_crc32':f'{stored:08x}'}


def main():
    env=os.environ.copy()
    env.setdefault('PLATFORMIO_CORE_DIR',str(ROOT/'analysis/platformio'))
    subprocess.run([sys.executable,'-m','platformio','run','--project-dir',str(ROOT/'firmware')],
                   env=env,check=True)
    source=ROOT/'firmware/.pio/build/idvn_esp8266/firmware.bin'
    data=source.read_bytes()
    checks=verify(data)
    output=ROOT/'build'; output.mkdir(exist_ok=True)
    target=output/'idvn-bridge.bin'
    shutil.copyfile(source,target)
    sources={str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted((ROOT/'firmware').rglob('*'))
        if path.is_file() and '.pio' not in path.parts and path.name!='local_config.h'}
    manifest={'file':target.name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
        'hardware_tested':False,'local_config_present':(ROOT/'firmware/include/local_config.h').exists(),
        'status':'development build; board/flash layout and electrical wiring require hardware validation',
        'platform':'espressif8266@4.2.1','arduino_core':'3.1.2','arduino_json':'6.21.5',
        'integrity':checks,'sources':sources}
    (output/'idvn-bridge.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'Exported {target}: {len(data)} bytes, SHA-256 {manifest["sha256"]}')
    print('Bootloader XOR, normalized application XOR, and Arduino CRC32 verified. No upload performed.')


if __name__=='__main__':
    main()
