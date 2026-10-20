#!/usr/bin/env python3
"""Run native protocol, HTTP, and API JSON tests under ASan/UBSan; build firmware first."""
from pathlib import Path
import os
import subprocess

root=Path(__file__).resolve().parents[1]
build=root/'build'
build.mkdir(exist_ok=True)
command=[os.environ.get('CXX','clang++'),'-std=c++11','-Wall','-Wextra','-Werror',
         '-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',
         '-I'+str(root/'firmware/lib/garo'),str(root/'tests/protocol_test.cpp'),
         str(root/'firmware/lib/garo/GaroProtocol.cpp'),'-o',str(build/'protocol_test')]
subprocess.run(command,check=True)
subprocess.run([str(build/'protocol_test')],check=True)
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++11','-Wall','-Wextra','-Werror',
    '-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',
    '-I'+str(root/'firmware/lib/tinyhttp'),str(root/'tests/http_request_test.cpp'),
    str(root/'firmware/lib/tinyhttp/HttpRequest.cpp'),'-o',str(build/'http_request_test')],check=True)
subprocess.run([str(build/'http_request_test')],check=True)
json_include=root/'firmware/.pio/libdeps/garo_esp8266/ArduinoJson/src'
if not json_include.exists():
    raise SystemExit('Build the firmware once to download the pinned ArduinoJson dependency before running JSON tests.')
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++11','-Wall','-Wextra','-Werror',
    '-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',
    '-I'+str(root/'firmware/lib/garo'),'-I'+str(root/'firmware/lib/garojson'),'-I'+str(json_include),
    str(root/'tests/json_settings_test.cpp'),str(root/'firmware/lib/garojson/GaroJson.cpp'),
    str(root/'firmware/lib/garo/GaroProtocol.cpp'),'-o',str(build/'json_settings_test')],check=True)
subprocess.run([str(build/'json_settings_test')],check=True)
