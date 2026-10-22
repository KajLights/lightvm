#!/usr/bin/env python3
"""Authenticated MQTT control latency and endurance on an attached controller.

Credentials are read from a private network configuration and never recorded.
Acknowledgements are timed at packet receipt, not at the end of a polling delay.
The result includes raw latencies, heap/stack samples and unsolicited commands.
"""
import argparse
import hashlib
import json
from pathlib import Path
import select
import socket
import struct
import time

from client import Client, SerialTransport
from benchmark_saved_preset import percentile


def string(value):
    data = value.encode()
    return struct.pack('!H', len(data)) + data


def packet(header, body=b''):
    count, length = len(body), bytearray()
    while True:
        length.append(count % 128 | (128 if count >= 128 else 0))
        count //= 128
        if not count:
            return bytes([header]) + length + body


class Broker:
    def __init__(self, config, controller):
        self.socket = socket.create_connection((config['host'], config['port']), timeout=5)
        self.socket.settimeout(5)
        flags = 2 | (128 if config.get('username') else 0) | (64 if config.get('password') else 0)
        body = string('MQTT') + bytes([4, flags]) + struct.pack('!H', 30) + string('lightvm-perf-' + str(time.time_ns()))
        for field in ('username', 'password'):
            if config.get(field):
                body += string(config[field])
        self.socket.sendall(packet(0x10, body))
        if self.exact(4) != b'\x20\x02\x00\x00':
            raise RuntimeError('broker rejected authentication')
        self.topic = 'homeassistant/lightvm/' + controller
        self.socket.sendall(packet(0x82, b'\x00\x01' + string(self.topic + '/#') + b'\x00'))
        self.states, self.commands, self.availability = [], [], []
        self.last_ping = time.monotonic()

    def exact(self, count):
        result = b''
        while len(result) < count:
            data = self.socket.recv(count - len(result))
            if not data:
                raise RuntimeError('broker disconnected')
            result += data
        return result

    def pump(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if time.monotonic() - self.last_ping > 10:
                self.socket.sendall(packet(0xc0))
                self.last_ping = time.monotonic()
            if not select.select([self.socket], [], [], min(.1, max(0, deadline - time.monotonic())))[0]:
                continue
            header, count, multiplier = self.exact(1)[0], 0, 1
            for _ in range(4):
                byte = self.exact(1)[0]
                count += (byte & 127) * multiplier
                multiplier *= 128
                if not byte & 128:
                    break
            else:
                raise RuntimeError('invalid MQTT remaining length')
            body = self.exact(count)
            received = time.monotonic()
            if header >> 4 == 3:
                length = int.from_bytes(body[:2], 'big')
                topic, value = body[2:2 + length].decode(), body[2 + length:]
                if topic == self.topic + '/lights/state':
                    # HA removes retained state with an empty publication when
                    # a test temporarily removes the output configuration.
                    if value:
                        self.states.append((received, json.loads(value)))
                elif topic == self.topic + '/lights/set':
                    self.commands.append((received, json.loads(value)))
                elif topic == self.topic + '/availability':
                    self.availability.append((received, value.decode()))

    def send(self, value, expected, timeout=20):
        first = len(self.states)
        started = time.monotonic()
        self.socket.sendall(packet(0x30, string(self.topic + '/lights/set') + json.dumps(value).encode()))
        while time.monotonic() - started < timeout:
            self.pump(.01)
            for received, state in self.states[first:]:
                if all(state.get(k) == v for k, v in expected.items()):
                    return {'command': value, 'expected': expected, 'state': state, 'latency_ms': (received - started) * 1000}
        raise RuntimeError('no matching applied state for ' + str(value) +
                           '; received states: ' + str(self.states[first:]))

    def close(self):
        self.socket.sendall(packet(0xe0))
        self.socket.close()


def run(args):
    client = Client(SerialTransport(args.port), timeout=30)
    # A freshly flashed controller may expose USB before it has subscribed to
    # MQTT. QoS 0 test commands sent in that window cannot be delivered.
    deadline = time.monotonic() + 60
    while not client.network_status()['mqtt_connected']:
        if time.monotonic() >= deadline:
            client.close()
            raise RuntimeError('controller did not connect to MQTT before testing')
        time.sleep(.25)
    description = client.describe()
    current = next(o for o in description['outputs'] if o['id'] == 'lights')
    if not current['selected_preset']:
        # Applying a saved output configuration need not recall its boot preset.
        config = next(o for o in client.get_config()['outputs'] if o['id'] == 'lights')
        preset = config.get('startup', {}).get('preset')
        if not preset:
            client.close()
            raise ValueError('HA stress needs an active or configured startup preset')
        client.recall_preset(preset, 'lights')
        time.sleep(7)
        description = client.describe()
    boot = description['boot_id']
    broker = Broker(json.loads(args.network.read_text())['mqtt'], description['controller_id'])
    output = next(o for o in description['outputs'] if o['id'] == 'lights')
    light = output['light']
    rgb = light['color']
    restore = {'state': 'ON' if light['power'] else 'OFF', 'effect': output['selected_preset'],
               'brightness': round(light['brightness'] * 255),
               'color': {'r': rgb >> 16, 'g': (rgb >> 8) & 255, 'b': rgb & 255},
               'color_mode': 'rgb'}
    result = {'label': args.label, 'firmware_sha256': hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
              'description_before': description, 'scenario': [], 'controls': [], 'soak': [], 'checks': [], 'output_retry_observations': []}
    started = time.monotonic()
    restored = False

    def checkpoint():
        args.result.write_text(json.dumps(result, indent=2) + '\n')

    def check():
        d = client.describe()
        n = client.network_status()
        status = client.status_fields(output='lights')
        errors = [o[key] for o in d['outputs'] for key in ('animation_error', 'light_error') if o.get(key)]
        retry_only = errors and all(error.endswith('; retrying') for error in errors)
        if retry_only:
            result['output_retry_observations'].append({'elapsed_s': time.monotonic() - started, 'errors': errors,
                                                        'output_frames': {o['id']: o['frames'] for o in d['outputs']}})
        if d['boot_id'] != boot or d['boot_error'] or (errors and not retry_only):
            result['failure'] = {'elapsed_s': time.monotonic() - started, 'description': d,
                                 'status': status, 'reason': 'reboot or animation failure'}
            checkpoint()
            raise RuntimeError(json.dumps({'boot_id': d['boot_id'], 'expected_boot_id': boot,
                                            'boot_error': d['boot_error'], 'outputs': [
                                                {k: o.get(k) for k in ('id', 'animation_error')} for o in d['outputs']]}))
        if not n['mqtt_connected'] or n['mqtt_error']:
            raise RuntimeError('MQTT failure')
        result['checks'].append({'elapsed_s': time.monotonic() - started, 'description': d,
                                 'heap': {k: int(status[k]) for k in ('heap', 'heap_peak', 'heap_limit')},
                                 'mqtt_connected': True})

    try:
        broker.pump(.3)
        result['scenario'].append(broker.send({'state': 'ON', 'effect': 'rainbow', 'brightness': 32},
                                              {'state': 'ON', 'effect': 'rainbow', 'brightness': 32}))
        for index in range(40):
            effect, brightness = ('stars' if index % 2 == 0 else 'rainbow'), (32 if index % 3 else 64)
            value = {'state': 'ON', 'effect': effect, 'brightness': brightness,
                     'color': {'r': 128 + index, 'g': 64, 'b': 32}, 'color_mode': 'rgb'}
            result['scenario'].append(broker.send(value, {'state': 'ON', 'effect': effect, 'brightness': brightness}))
            if index % 10 == 9:
                check(); checkpoint()
            broker.pump(.08)
        for _ in range(10):
            for state in ('OFF', 'ON'):
                result['scenario'].append(broker.send({'state': state}, {'state': state}))
                broker.pump(.08)
        result['scenario'].append(broker.send({'state': 'ON', 'effect': 'stars', 'brightness': 32},
                                              {'state': 'ON', 'effect': 'stars', 'brightness': 32}))
        check(); checkpoint()
        print('62-command recall/power scenario passed.', flush=True)
        for index in range(args.controls):
            brightness = 64 if index % 2 == 0 else 32
            result['controls'].append(broker.send({'brightness': brightness}, {'brightness': brightness}))
            if index % 50 == 49:
                check(); checkpoint()
                print(json.dumps({'alternating_controls': index + 1, 'last_latency_ms': result['controls'][-1]['latency_ms']}), flush=True)
            broker.pump(.03)
        soak_start = time.monotonic()
        next_action = soak_start
        while time.monotonic() - soak_start < args.soak_seconds:
            broker.pump(min(1, args.soak_seconds - (time.monotonic() - soak_start)))
            if time.monotonic() >= next_action:
                index = len(result['soak'])
                effect, brightness = ('stars' if index % 2 == 0 else 'rainbow'), (64 if index % 3 else 32)
                command = {'state': 'ON', 'effect': effect, 'brightness': brightness}
                row = broker.send(command, command)
                row['elapsed_s'] = time.monotonic() - soak_start
                result['soak'].append(row)
                check(); checkpoint()
                print(json.dumps({'soak_seconds': round(row['elapsed_s']), 'checks': len(result['checks'])}), flush=True)
                next_action += 30
        result['soak_seconds'] = time.monotonic() - soak_start
        broker.send(restore, {k: restore[k] for k in ('state', 'effect', 'brightness')})
        restored = True
        broker.pump(7)
        check()
        result['availability'] = broker.availability
        result['observed_commands'] = broker.commands
        result['boot_unchanged'] = True
        for name in ('scenario', 'controls', 'soak'):
            values = [row['latency_ms'] for row in result[name]]
            if values:
                result[name + '_summary'] = {'count': len(values), 'p95_ms': percentile(values, .95),
                                             'p99_ms': percentile(values, .99), 'max_ms': max(values)}
        result['complete'] = True
        result['error_free'] = not result['output_retry_observations']
        checkpoint()
        print(json.dumps({**{k: v for k, v in result.items() if k.endswith('_summary') or k in ('soak_seconds', 'error_free')},
                          'output_retry_observations': len(result['output_retry_observations'])}), flush=True)
    finally:
        try:
            if not restored:
                broker.send(restore, {k: restore[k] for k in ('state', 'effect', 'brightness')})
        finally:
            broker.close()
            client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--network', required=True, type=Path, help='private full network configuration; never copied into evidence')
    parser.add_argument('--firmware', required=True, type=Path)
    parser.add_argument('--label', required=True)
    parser.add_argument('--result', required=True, type=Path)
    parser.add_argument('--controls', type=int, default=1000)
    parser.add_argument('--soak-seconds', type=float, default=0)
    args = parser.parse_args()
    if args.controls < 0 or args.soak_seconds < 0:
        parser.error('counts/durations must be nonnegative')
    run(args)


if __name__ == '__main__':
    main()
