#!/usr/bin/env python3
"""Live check of the real MqttBridge against a local Mosquitto broker.

Requires mosquitto, mosquitto_sub and mosquitto_pub on PATH and build/live_bridge
(see tools/test_firmware.py --live). The controller is simulated; no hardware.
"""
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

root = pathlib.Path(__file__).resolve().parents[2]
PORT = 18830
PREFIX = 'hotrod/idvn'
broker_bin = shutil.which('mosquitto') or '/opt/homebrew/opt/mosquitto/sbin/mosquitto'


def wait_for(predicate, timeout=15, message='timeout'):
    end = time.time() + timeout
    while time.time() < end:
        value = predicate()
        if value:
            return value
        time.sleep(0.1)
    raise AssertionError(message)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        conf = pathlib.Path(tmp) / 'mosquitto.conf'
        conf.write_text(f'listener {PORT} 127.0.0.1\nallow_anonymous true\n')
        broker = subprocess.Popen([broker_bin, '-c', str(conf)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        procs = [broker]
        try:
            time.sleep(1)
            log = open(pathlib.Path(tmp) / 'sub.log', 'w+')
            procs.append(subprocess.Popen(['mosquitto_sub', '-p', str(PORT), '-t', PREFIX + '/#', '-v'], stdout=log, bufsize=0))
            time.sleep(0.5)
            bridge = subprocess.Popen([str(root / 'build/live_bridge'), '25'], stdout=subprocess.PIPE, text=True)
            procs.append(bridge)

            def messages():
                log.flush()
                log.seek(0)
                out = []
                for line in log.read().splitlines():
                    topic, _, payload = line.partition(' ')
                    out.append((topic, payload))
                return out

            def last(suffix):
                found = [p for t, p in messages() if t == f'{PREFIX}/{suffix}']
                return found[-1] if found else None

            wait_for(lambda: last('availability') == 'online', message='bridge never came online')
            state = json.loads(wait_for(lambda: last('state'), message='no state published'))
            session = state['session']
            assert state['temperature_c'] is None and state['capabilities']['start_now'] is False
            assert state['controller_online'] is True, state
            print('connected, retained state seen, session', session)

            def send(payload, retain=False):
                cmd = ['mosquitto_pub', '-p', str(PORT), '-t', PREFIX + '/command', '-q', '1', '-m', json.dumps(payload)]
                if retain:
                    cmd.append('-r')
                subprocess.run(cmd, check=True)

            def results():
                return [json.loads(p) for t, p in messages() if t == f'{PREFIX}/result']

            def result_for(i, session_id=None):
                return [r for r in results() if r.get('id') == i]

            mode = {'left': 0, 'right': 2, 'timers': 1, 'period': 0}
            send({'session': session, 'id': 1, 'command': 'mode', 'value': mode})
            wait_for(lambda: any(r['status'] == 'applied' for r in result_for(1)), message='mode not applied')
            send({'session': session, 'id': 1, 'command': 'mode', 'value': mode})
            wait_for(lambda: any(r.get('error') == 'duplicate_or_old_request' for r in result_for(1)))
            send({'session': session, 'id': 2, 'command': 'start_now'})
            wait_for(lambda: any(r.get('error') == 'controller_extension_required' for r in result_for(2)))
            send({'session': 'bad', 'id': 3, 'command': 'identify'})
            wait_for(lambda: any(r.get('error') == 'invalid_session' for r in results()))
            send({'session': session, 'id': 4, 'command': 'mode', 'value': {'left': 9}})
            wait_for(lambda: any(r.get('error') == 'invalid_or_incomplete_settings' for r in result_for(4)))
            # The broker clears the retain flag when forwarding to an already-subscribed client,
            # so this executes. Retained-at-subscribe rejection is covered by the native tests.
            send({'session': session, 'id': 5, 'command': 'identify'}, retain=True)
            wait_for(lambda: any(r['status'] == 'applied' for r in result_for(5)), message='identify not applied')
            print('command results OK:', [(r.get('id'), r.get('status'), r.get('error')) for r in results()])
            subprocess.run(['mosquitto_pub', '-p', str(PORT), '-t', PREFIX + '/command', '-r', '-n'], check=True)

            out, _ = bridge.communicate(timeout=40)
            print(out.strip())
            assert 'controller_writes=2' in out, out
            # Last will after the bridge exits abruptly is only sent on unclean disconnect;
            # the bridge ends the process without DISCONNECT, so the broker publishes it.
            wait_for(lambda: last('availability') == 'offline', timeout=10, message='last will not delivered')
            print('last will delivered; live broker test passed')
        finally:
            for p in procs:
                if p.poll() is None:
                    p.kill()


if __name__ == '__main__':
    sys.exit(main())
