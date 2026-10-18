"""Real engine + TCP MQTT wire integration; no Python MQTT dependency required."""
import copy
import json
import os
from pathlib import Path
import queue
import socket
import struct
import sys
import tempfile
import threading
import time
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from client import Client, HttpTransport, ProcessTransport, ProtocolError, STOP
from lightvm.compiler import compile_source


def packet(header, body=b''):
    n = len(body)
    length = bytearray()
    while True:
        length.append(n % 128 | (128 if n >= 128 else 0))
        n //= 128
        if not n:
            return bytes([header]) + length + body


def string(value):
    value = value.encode()
    return struct.pack('!H', len(value)) + value


class Broker:
    """Strict observable peer: acknowledge handshake and record publications."""
    def __init__(self):
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen()
        self.listener.settimeout(.1)
        self.port = self.listener.getsockname()[1]
        self.connection = None
        self.lock = threading.Lock()
        self.messages = queue.Queue()
        self.connects = queue.Queue()
        self.subscriptions = queue.Queue()
        self.latest = {}
        self.history = []
        self.errors = []
        self.stopping = False
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stopping:
            try:
                connection, _ = self.listener.accept()
                self.connection = connection
                connection.settimeout(.1)
                data = bytearray()
                while not self.stopping:
                    try:
                        chunk = connection.recv(8192)
                    except socket.timeout:
                        continue
                    if not chunk:
                        break
                    data += chunk
                    while len(data) >= 2:
                        n, multiplier, offset = 0, 1, 1
                        while offset < len(data):
                            b = data[offset]
                            offset += 1
                            n += (b & 127) * multiplier
                            multiplier *= 128
                            if not b & 128:
                                break
                        else:
                            break
                        if len(data) < offset + n:
                            break
                        header, body = data[0], bytes(data[offset:offset+n])
                        del data[:offset+n]
                        if header == 0x10:
                            self.connects.put(body)
                            self.send(packet(0x20, b'\x00\x00'))
                        elif header == 0x82:
                            size = int.from_bytes(body[2:4], 'big')
                            self.subscriptions.put(body[4:4+size].decode())
                            self.send(packet(0x90, body[:2] + b'\x00'))
                        elif header >> 4 == 3:
                            size = int.from_bytes(body[:2], 'big')
                            item = (body[2:2+size].decode(), body[2+size:], bool(header & 1))
                            self.messages.put(item)
                        elif header == 0xc0:
                            self.send(packet(0xd0))
                        elif header == 0x40:
                            pass
                        else:
                            raise AssertionError(f'unexpected client header {header:x}')
            except (socket.timeout, ConnectionError, OSError):
                continue
            except Exception as error:
                self.errors.append(error)
            finally:
                if self.connection:
                    self.connection.close()
                    self.connection = None

    def send(self, data):
        with self.lock:
            self.connection.sendall(data)

    def publish(self, topic, payload, retained=False, qos=0):
        if isinstance(payload, str):
            payload = payload.encode()
        self.send(packet(0x30 | int(retained) | (qos << 1), string(topic) + (b'\x00\x07' if qos else b'') + payload))

    def wait(self, topic, value=None, timeout=4):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                item = self.messages.get(timeout=.05)
            except queue.Empty:
                continue
            self.history.append(item)
            self.latest[item[0]] = item[1]
            if item[0] == topic and (value is None or item[1] == value):
                return item[1]
        raise AssertionError(f'no publication {topic}={value!r}; received {self.history[-10:]}')

    def close(self):
        self.stopping = True
        self.listener.close()
        if self.connection:
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        self.thread.join(2)


class MqttTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.receiver = Path(os.environ.get('LIGHTVM_RECEIVER', ROOT / 'build/mqtt-host/lightvm'))
        if not cls.receiver.exists():
            raise unittest.SkipTest('Build a host receiver with LIGHTVM_MQTT=ON')
        probe = Client(ProcessTransport(cls.receiver))
        try:
            if not probe.describe().get('mqtt_supported'):
                raise unittest.SkipTest('MQTT excluded from build')
        finally:
            probe.close()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.storage = Path(self.directory.name) / 'store.bin'
        self.broker = Broker()
        self.client = Client(ProcessTransport(self.receiver, self.storage))
        self.config = {'schema': 'lightvm.config', 'outputs': [
            {'id': 'desk', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': 5},
             'home_assistant': {'name': 'Desk "light" 🌈', 'default_preset': 'blue', 'presets': ['blue']}},
            {'id': 'other', 'type': 'ws2812b', 'config': {'pin': 'GP17', 'count': 5},
             'home_assistant': {'default_preset': 'blue', 'presets': ['blue']}}]}
        self.client.configure(self.config, save=True)
        program = compile_source('''from lightvm import profile, init, render, rgb
profile("animation")
@init
def setup(ctx, runtime):
 pass
@render
def draw(ctx, runtime, pixels):
 pixels.fill(rgb(0,0,255))
''').bytecode
        self.client.save_program('brush', program)
        self.client.save_preset('blue', [{'id': 'base', 'program': 'brush', 'crc32': zlib.crc32(program)}], output='desk')
        self.network = {'schema': 'lightvm.network', 'wifi': {'enabled': False, 'networks': []},
                        'http': {'enabled': False}, 'mqtt': {'enabled': True, 'host': 'localhost',
                        'port': self.broker.port, 'username': 'kaj', 'password': 'broker-secret'}}
        self.client.configure_network(self.network)
        self.controller_id = self.client.describe()['controller_id']
        self.root = f'homeassistant/lightvm/{self.controller_id}'
        self.discovery = f'homeassistant/light/{self.controller_id}/{b"desk".hex()}/config'
        self.selector = f'homeassistant/select/{self.controller_id}/{b"desk".hex()}_preset/config'
        self.broker.wait(f'{self.root}/availability', b'online')

    def tearDown(self):
        self.client.close()
        self.broker.close()
        self.assertFalse(self.broker.errors)
        self.directory.cleanup()

    def wait_options(self, expected):
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            value = json.loads(self.broker.wait(self.selector, timeout=max(.01, deadline - time.monotonic())))
            if value['options'] == expected:
                return value
        self.fail(f'no preset options {expected}')

    def test_preset_selector_discovery_selection_and_power_recall(self):
        selector = json.loads(self.broker.latest[self.selector])
        self.assertEqual(selector['name'], 'Desk "light" 🌈 Preset')
        self.assertEqual(selector['options'], ['blue'])
        self.assertEqual(selector['command_topic'], f'{self.root}/desk/preset/set')
        self.assertEqual(selector['state_topic'], f'{self.root}/desk/preset/state')
        self.assertFalse(selector['retain'])
        self.assertEqual(selector['device'], json.loads(self.broker.latest[self.discovery])['device'])
        layers = self.client.preset('blue')['layers']
        layers[0]['opacity'] = 100
        self.client.save_preset('dim', layers, output='desk')
        self.config['outputs'][0]['home_assistant']['presets'].append('dim')
        self.client.configure(self.config)
        self.wait_options(['blue', 'dim'])
        self.broker.publish(f'{self.root}/desk/preset/set', 'dim', qos=1)
        self.broker.wait(f'{self.root}/desk/state', b'ON')
        self.broker.wait(f'{self.root}/desk/preset/state', b'dim')
        composition = self.client.composition('desk')
        self.assertEqual(composition['preset'], 'dim')
        self.assertFalse(self.client.composition('other')['active'])
        self.broker.publish(f'{self.root}/desk/preset/set', 'dim', qos=1)
        time.sleep(.05)
        self.assertEqual(self.client.composition('desk')['revision'], composition['revision'])
        self.broker.publish(f'{self.root}/desk/set', 'OFF')
        self.broker.wait(f'{self.root}/desk/state', b'OFF')
        self.assertEqual(self.client.composition('desk')['preset'], 'dim')
        self.broker.publish(f'{self.root}/desk/set', 'ON')
        self.broker.wait(f'{self.root}/desk/state', b'ON')
        self.broker.wait(f'{self.root}/desk/preset/state', b'dim')
        self.assertEqual(self.client.composition('desk')['preset'], 'dim')
        self.broker.publish(f'{self.root}/other/preset/set', 'dim')
        time.sleep(.05)
        self.assertFalse(self.client.composition('other')['active'])

    def test_preset_options_follow_catalog_and_binding_changes(self):
        self.config['outputs'][0]['home_assistant']['presets'].append('later')
        self.client.configure(self.config)
        self.wait_options(['blue'])
        self.client.save_preset('later', self.client.preset('blue')['layers'], output='desk')
        self.wait_options(['blue', 'later'])
        self.client.delete_preset('later')
        self.wait_options(['blue'])
        self.config['outputs'][0]['home_assistant']['presets'] = ['later']
        self.config['outputs'][0]['home_assistant']['default_preset'] = 'later'
        self.client.configure(self.config)
        self.wait_options([])

    def test_preset_state_observes_serial_edits_and_catalog_versions(self):
        self.client.activate_preset('blue', output='desk')
        self.broker.wait(f'{self.root}/desk/preset/state', b'blue')
        self.client.layer_opacity('base', 100, output='desk')
        self.broker.wait(f'{self.root}/desk/preset/state', b'None')
        self.assertIsNone(self.client.composition('desk')['preset'])
        self.client.activate_preset('blue', output='desk')
        self.broker.wait(f'{self.root}/desk/preset/state', b'blue')
        layers = self.client.preset('blue')['layers']
        layers[0]['opacity'] = 200
        self.client.save_preset('blue', layers, output='desk')
        self.broker.wait(f'{self.root}/desk/preset/state', b'None')
        self.assertTrue(self.client.composition('desk')['playing'])
        self.assertIsNone(self.client.describe()['outputs'][0]['preset'])

    def test_retained_preset_and_failed_selection_preserve_current_runtime(self):
        self.client.activate_preset('blue', output='desk')
        self.broker.wait(f'{self.root}/desk/preset/state', b'blue')
        before = self.client.composition('desk')['revision']
        for payload in ('blue', 'unknown', 'bad/name', b'blue\x00junk'):
            self.broker.publish(f'{self.root}/desk/preset/set', payload, retained=True)
        self.config['outputs'][0]['home_assistant']['presets'].append('missing')
        self.client.configure(self.config)
        self.broker.publish(f'{self.root}/desk/preset/set', 'missing')
        self.broker.wait(f'{self.root}/desk/preset/state', b'blue')
        self.assertEqual(self.client.composition('desk')['revision'], before)
        self.assertIn('does not exist', self.client.network_status()['mqtt_error'])

    def test_preset_identity_across_ab_mix_and_initialization_reset(self):
        self.client.activate_preset('blue', output='desk', slot='A')
        self.client.activate_preset('blue', output='desk', slot='B')
        self.broker.wait(f'{self.root}/desk/preset/state', b'blue')
        self.client.set_progress(50, output='desk')
        self.broker.wait(f'{self.root}/desk/preset/state', b'None')
        self.client.set_progress(100, output='desk')
        self.broker.wait(f'{self.root}/desk/preset/state', b'blue')
        with self.assertRaises(ProtocolError):
            self.client.reset_program({'unknown': 1}, output='desk', slot='B', layer='base')
        self.assertEqual(self.client.composition('desk', slot='B')['preset'], 'blue')
        self.client.reset_program(output='desk', slot='B', layer='base')
        self.broker.wait(f'{self.root}/desk/preset/state', b'None')

    def test_maximum_selector_options_and_escaped_name_fit_mqtt(self):
        names = [f'p{i:031d}' for i in range(16)]
        layers = self.client.preset('blue')['layers']
        for name in names:
            self.client.save_preset(name, layers, output='desk')
        output = self.config['outputs'][0]
        output['id'] = 'x' * 32
        output['home_assistant'] = {'name': '\x01' * 64, 'default_preset': names[0], 'presets': names}
        self.client.configure(self.config)
        topic = f'homeassistant/select/{self.controller_id}/{output["id"].encode().hex()}_preset/config'
        payload = json.loads(self.broker.wait(topic))
        self.assertEqual(payload['options'], names)
        self.assertEqual(payload['name'], '\x01' * 64 + ' Preset')
        self.broker.publish(f'{self.root}/{output["id"]}/preset/set', names[-1])
        self.broker.wait(f'{self.root}/{output["id"]}/preset/state', names[-1].encode())

    def test_ha_binding_validation(self):
        for changes in ({'presets': []}, {'presets': ['blue', 'blue']}, {'presets': ['other']},
                        {'presets': ['blue', 'bad/name']}, {'presets': ['blue', 'None']},
                        {'presets': ['blue'] + [f'p{i}' for i in range(16)]},
                        {'default_preset': 'x' * 33}):
            config = copy.deepcopy(self.config)
            config['outputs'][0]['home_assistant'].update(changes)
            with self.assertRaises(ProtocolError):
                self.client.configure(config)

    def test_discovery_and_independent_on_off_with_real_presets(self):
        discovery = json.loads(self.broker.latest[self.discovery])
        self.assertEqual(discovery['name'], 'Desk "light" 🌈')
        self.assertEqual(discovery['command_topic'], f'{self.root}/desk/set')
        self.assertNotIn('brightness_command_topic', discovery)
        self.assertNotIn('password', self.client.network_config()['mqtt'])
        connect = self.broker.connects.get(timeout=1)
        self.assertEqual(connect[:7], b'\x00\x04MQTT\x04')
        self.assertEqual(connect[7], 0xe6)
        self.assertIn(b'offline', connect)
        self.assertIn(b'broker-secret', connect)
        self.broker.publish(f'{self.root}/desk/set', 'ON', qos=1)
        self.broker.wait(f'{self.root}/desk/state', b'ON')
        composition = self.client.composition('desk')
        self.assertTrue(composition['playing'])
        self.assertFalse(self.client.composition('other')['active'])
        self.broker.publish(f'{self.root}/desk/set', 'ON', qos=1)
        time.sleep(.05)
        self.assertEqual(self.client.composition('desk')['revision'], composition['revision'])
        self.broker.publish(f'{self.root}/desk/set', 'OFF')
        self.broker.wait(f'{self.root}/desk/state', b'OFF')
        self.assertFalse(self.client.composition('desk')['playing'])
        self.assertEqual(self.client.composition_snapshot(output='desk')[1], bytes(15))
        self.broker.publish(f'{self.root}/other/set', 'ON')
        self.broker.wait(f'{self.root}/other/state', b'ON')
        self.assertTrue(self.client.composition('other')['playing'])
        self.client.require_ok(self.client.command(STOP, output='other'))
        self.broker.wait(f'{self.root}/other/state', b'OFF')

    def test_http_websocket_and_serial_changes_are_published_to_mqtt(self):
        if not self.client.describe()['http_supported']:
            self.skipTest('HTTP excluded')
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        self.network['http'] = {'enabled': True, 'port': port}
        self.client.configure_network(self.network)
        self.broker.wait(f'{self.root}/availability', b'online')
        url = f'http://127.0.0.1:{port}'
        peers = [Client(HttpTransport(url))]
        try:
            if self.client.describe().get('websocket_supported'):
                from test_websocket import Wire
                peers.append(Client(Wire(url)))
            for peer in peers:
                self.broker.publish(f'{self.root}/desk/set', 'ON')
                self.broker.wait(f'{self.root}/desk/state', b'ON')
                self.assertTrue(peer.composition('desk')['playing'])
                peer.require_ok(peer.command(STOP, output='desk'))
                self.broker.wait(f'{self.root}/desk/state', b'OFF')
                self.assertEqual(self.client.composition_snapshot(output='desk')[1], bytes(15))
        finally:
            for peer in peers:
                peer.close()

    def test_discovery_identifier_accepts_dots_without_collisions(self):
        self.config['outputs'][0]['id'] = 'desk.v1'
        self.config['outputs'][1]['id'] = 'desk_2ev1'
        self.client.configure(self.config)
        first = f'homeassistant/light/{self.controller_id}/{b"desk.v1".hex()}/config'
        second = f'homeassistant/light/{self.controller_id}/{b"desk_2ev1".hex()}/config'
        self.broker.wait(first)
        if second not in self.broker.latest:
            self.broker.wait(second)
        self.assertNotEqual(first, second)
        self.assertEqual(json.loads(self.broker.latest[first])['command_topic'], f'{self.root}/desk.v1/set')

    def test_on_replaces_a_stopped_ab_presenter(self):
        self.client.activate_preset('blue', output='desk', slot='A')
        self.client.activate_preset('blue', output='desk', slot='B')
        self.client.set_progress(100, output='desk')
        self.client.require_ok(self.client.command(STOP, output='desk'))
        self.broker.wait(f'{self.root}/desk/state', b'OFF')
        self.broker.publish(f'{self.root}/desk/set', 'ON')
        self.broker.wait(f'{self.root}/desk/state', b'ON')
        self.assertFalse(self.client.transition(output='desk')['active'])
        self.assertTrue(self.client.composition('desk')['playing'])
        self.assertEqual(self.client.composition_snapshot(output='desk')[1], bytes([0, 0, 255]) * 5)

    def test_retained_invalid_unknown_commands_and_failed_preset(self):
        for topic, payload, retained in [('desk', 'ON', True), ('desk', 'TOGGLE', False), ('unknown', 'ON', False)]:
            self.broker.publish(f'{self.root}/{topic}/set', payload, retained=retained)
        time.sleep(.1)
        self.assertFalse(self.client.composition('desk')['active'])
        self.config['outputs'][0]['home_assistant']['default_preset'] = 'missing'
        self.config['outputs'][0]['home_assistant']['presets'] = ['missing']
        self.client.configure(self.config)
        self.broker.publish(f'{self.root}/desk/set', 'ON')
        self.broker.wait(f'{self.root}/desk/state', b'OFF')
        self.assertIn('does not exist', self.client.network_status()['mqtt_error'])
        self.assertFalse(self.client.composition('desk')['active'])

    def test_reconnect_birth_removal_and_saved_settings(self):
        self.broker.publish('homeassistant/status', 'online')
        self.broker.wait(self.discovery)
        self.broker.wait(f'{self.root}/availability', b'online')
        self.broker.connection.shutdown(socket.SHUT_RDWR)
        self.broker.wait(f'{self.root}/availability', b'online')
        self.assertTrue(self.client.network_status()['mqtt_connected'])
        self.client.close()
        self.client = Client(ProcessTransport(self.receiver, self.storage))
        self.assertEqual(self.client.describe()['controller_id'], self.controller_id)
        self.broker.wait(f'{self.root}/availability', b'online')
        self.broker.publish(f'{self.root}/desk/set', 'ON')
        self.broker.wait(f'{self.root}/desk/state', b'ON')
        del self.config['outputs'][0]['home_assistant']
        self.client.configure(self.config)
        self.broker.wait(self.discovery, b'')
        self.broker.wait(self.selector, b'')
        self.broker.wait(f'{self.root}/desk/state', b'')
        self.broker.wait(f'{self.root}/desk/preset/state', b'')
        self.broker.publish(f'{self.root}/desk/set', 'OFF')
        time.sleep(.05)
        self.assertTrue(self.client.composition('desk')['playing'])

    def test_validation_and_secret_preservation(self):
        for field, value in [('host', 'http://broker.local'), ('host', 'broker.local:1883'), ('host', 'a..b'), ('host', '256.0.0.1'), ('port', 0), ('enabled', 'yes'), ('extra', 1)]:
            config = copy.deepcopy(self.network)
            config['mqtt'][field] = value
            with self.assertRaises(ProtocolError):
                self.client.configure_network(config, validate=True)
        config = copy.deepcopy(self.network)
        del config['mqtt']['password']
        self.client.configure_network(config)
        self.broker.wait(f'{self.root}/availability', b'online')
        self.broker.connects.get(timeout=1)
        self.assertIn(b'broker-secret', self.broker.connects.get(timeout=1))
        config = copy.deepcopy(self.config)
        config['outputs'][0]['home_assistant']['default_preset'] = 'bad/preset'
        with self.assertRaises(ProtocolError):
            self.client.configure(config)


if __name__ == '__main__':
    unittest.main()
