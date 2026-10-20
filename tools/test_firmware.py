#!/usr/bin/env python3
"""Run native protocol, HTTP, and API JSON tests under ASan/UBSan; build firmware first."""
from pathlib import Path
import os
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
build=root/'build'
build.mkdir(exist_ok=True)
command=[os.environ.get('CXX','clang++'),'-std=c++11','-Wall','-Wextra','-Werror',
         '-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',
         '-I'+str(root/'firmware/lib/idvn'),str(root/'tests/protocol_test.cpp'),
         str(root/'firmware/lib/idvn/IdvnProtocol.cpp'),'-o',str(build/'protocol_test')]
subprocess.run(command,check=True)
subprocess.run([str(build/'protocol_test')],check=True)
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++11','-Wall','-Wextra','-Werror',
    '-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',
    '-I'+str(root/'firmware/lib/tinyhttp'),str(root/'tests/http_request_test.cpp'),
    str(root/'firmware/lib/tinyhttp/HttpRequest.cpp'),'-o',str(build/'http_request_test')],check=True)
subprocess.run([str(build/'http_request_test')],check=True)
json_include=root/'firmware/.pio/libdeps/idvn_esp8266/ArduinoJson/src'
if not json_include.exists():
    raise SystemExit('Build the firmware once to download the pinned ArduinoJson dependency before running JSON tests.')
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++11','-Wall','-Wextra','-Werror',
    '-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',
    '-I'+str(root/'firmware/lib/idvn'),'-I'+str(root/'firmware/lib/idvnjson'),'-I'+str(json_include),
    str(root/'tests/json_settings_test.cpp'),str(root/'firmware/lib/idvnjson/IdvnJson.cpp'),
    str(root/'firmware/lib/idvn/IdvnProtocol.cpp'),'-o',str(build/'json_settings_test')],check=True)
subprocess.run([str(build/'json_settings_test')],check=True)
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++11','-Wall','-Wextra','-Werror',
    '-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',
    '-I'+str(root/'firmware/lib/idvn'),'-I'+str(root/'firmware/lib/idvnjson'),
    '-I'+str(root/'firmware/lib/mqtt'),'-I'+str(json_include),
    str(root/'tests/mqtt_test.cpp'),str(root/'firmware/lib/mqtt/MqttWire.cpp'),
    str(root/'firmware/lib/idvnjson/MqttCommand.cpp'),str(root/'firmware/lib/idvnjson/IdvnJson.cpp'),
    str(root/'firmware/lib/idvn/IdvnProtocol.cpp'),'-o',str(build/'mqtt_test')],check=True)
subprocess.run([str(build/'mqtt_test')],check=True)
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++11','-Wall','-Wextra','-Werror',
    '-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',
    '-DIDVN_MQTT_IP="192.0.2.1"',
    '-I'+str(root/'tests/mqtt_fakes'),'-I'+str(root/'firmware/include'),
    '-I'+str(root/'firmware/lib/idvn'),'-I'+str(root/'firmware/lib/idvnjson'),
    '-I'+str(root/'firmware/lib/mqtt'),'-I'+str(json_include),
    str(root/'tests/mqtt_bridge_test.cpp'),str(root/'firmware/src/MqttBridge.cpp'),
    str(root/'firmware/lib/mqtt/MqttWire.cpp'),str(root/'firmware/lib/idvnjson/MqttCommand.cpp'),
    str(root/'firmware/lib/idvnjson/IdvnJson.cpp'),str(root/'firmware/lib/idvn/IdvnProtocol.cpp'),
    '-o',str(build/'mqtt_bridge_test')],check=True)
subprocess.run([str(build/'mqtt_bridge_test')],check=True)
if '--live' in sys.argv:
    # Optional: real MqttBridge over TCP to a local Mosquitto (must be installed).
    subprocess.run([os.environ.get('CXX','clang++'),'-std=c++14','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-g','-DIDVN_MQTT_IP="127.0.0.1"','-DIDVN_MQTT_PORT=18830',
        '-I'+str(root/'tests/mqtt_live'),'-I'+str(root/'firmware/include'),
        '-I'+str(root/'firmware/lib/idvn'),'-I'+str(root/'firmware/lib/idvnjson'),
        '-I'+str(root/'firmware/lib/mqtt'),'-I'+str(json_include),
        str(root/'tests/mqtt_live/live_bridge.cpp'),str(root/'firmware/src/MqttBridge.cpp'),
        str(root/'firmware/lib/mqtt/MqttWire.cpp'),str(root/'firmware/lib/idvnjson/MqttCommand.cpp'),
        str(root/'firmware/lib/idvnjson/IdvnJson.cpp'),str(root/'firmware/lib/idvn/IdvnProtocol.cpp'),
        '-o',str(build/'live_bridge')],check=True)
    subprocess.run([sys.executable,str(root/'tests/mqtt_live/run_live.py')],check=True)
