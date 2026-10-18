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
        self.config = {'schema': 'lightvm.config', 'devices': [
            {'id': 'desk', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': 5},
             'home_assistant': {'name': 'Desk "light" 🌈', 'on_preset': 'blue'}},
            {'id': 'other', 'type': 'ws2812b', 'config': {'pin': 'GP17', 'count': 5},
             'home_assistant': {'on_preset': 'blue'}}]}
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
        self.client.save_procedure('brush', program)
        self.client.save_preset('blue', [{'id': 'base', 'procedure': 'brush', 'crc32': zlib.crc32(program)}], device='desk')
        self.network = {'schema': 'lightvm.network', 'wifi': {'enabled': False, 'networks': []},
                        'http': {'enabled': False}, 'mqtt': {'enabled': True, 'host': '127.0.0.1',
                        'port': self.broker.port, 'username': 'kaj', 'password': 'broker-secret'}}
        self.client.configure_network(self.network)
        self.device_id = self.client.describe()['device_id']
        self.root = f'lightvm/{self.device_id}'
        self.discovery = f'homeassistant/light/{self.device_id}/{b"desk".hex()}/config'
        self.broker.wait(f'{self.root}/availability', b'online')

    def tearDown(self):
        self.client.close()
        self.broker.close()
        self.assertFalse(self.broker.errors)
        self.directory.cleanup()

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
        scene = self.client.scene('desk')
        self.assertTrue(scene['playing'])
        self.assertFalse(self.client.scene('other')['active'])
        self.broker.publish(f'{self.root}/desk/set', 'ON', qos=1)
        time.sleep(.05)
        self.assertEqual(self.client.scene('desk')['revision'], scene['revision'])
        self.broker.publish(f'{self.root}/desk/set', 'OFF')
        self.broker.wait(f'{self.root}/desk/state', b'OFF')
        self.assertFalse(self.client.scene('desk')['playing'])
        self.assertEqual(self.client.scene_snapshot(device='desk')[1], bytes(15))
        self.broker.publish(f'{self.root}/other/set', 'ON')
        self.broker.wait(f'{self.root}/other/state', b'ON')
        self.assertTrue(self.client.scene('other')['playing'])
        self.client.require_ok(self.client.command(STOP, device='other'))
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
                self.assertTrue(peer.scene('desk')['playing'])
                peer.require_ok(peer.command(STOP, device='desk'))
                self.broker.wait(f'{self.root}/desk/state', b'OFF')
                self.assertEqual(self.client.scene_snapshot(device='desk')[1], bytes(15))
        finally:
            for peer in peers:
                peer.close()

    def test_discovery_identifier_accepts_dots_without_collisions(self):
        self.config['devices'][0]['id'] = 'desk.v1'
        self.config['devices'][1]['id'] = 'desk_2ev1'
        self.client.configure(self.config)
        first = f'homeassistant/light/{self.device_id}/{b"desk.v1".hex()}/config'
        second = f'homeassistant/light/{self.device_id}/{b"desk_2ev1".hex()}/config'
        self.broker.wait(first)
        if second not in self.broker.latest:
            self.broker.wait(second)
        self.assertNotEqual(first, second)
        self.assertEqual(json.loads(self.broker.latest[first])['command_topic'], f'{self.root}/desk.v1/set')

    def test_on_replaces_a_stopped_ab_presenter(self):
        self.client.activate_preset('blue', device='desk', slot='A')
        self.client.activate_preset('blue', device='desk', slot='B')
        self.client.set_progress(100, device='desk')
        self.client.require_ok(self.client.command(STOP, device='desk'))
        self.broker.wait(f'{self.root}/desk/state', b'OFF')
        self.broker.publish(f'{self.root}/desk/set', 'ON')
        self.broker.wait(f'{self.root}/desk/state', b'ON')
        self.assertFalse(self.client.transition(device='desk')['active'])
        self.assertTrue(self.client.scene('desk')['playing'])
        self.assertEqual(self.client.scene_snapshot(device='desk')[1], bytes([0, 0, 255]) * 5)

    def test_retained_invalid_unknown_commands_and_failed_preset(self):
        for topic, payload, retained in [('desk', 'ON', True), ('desk', 'TOGGLE', False), ('unknown', 'ON', False)]:
            self.broker.publish(f'{self.root}/{topic}/set', payload, retained=retained)
        time.sleep(.1)
        self.assertFalse(self.client.scene('desk')['active'])
        self.config['devices'][0]['home_assistant']['on_preset'] = 'missing'
        self.client.configure(self.config)
        self.broker.publish(f'{self.root}/desk/set', 'ON')
        self.broker.wait(f'{self.root}/desk/state', b'OFF')
        self.assertIn('does not exist', self.client.network_status()['mqtt_error'])
        self.assertFalse(self.client.scene('desk')['active'])

    def test_reconnect_birth_removal_and_saved_settings(self):
        self.broker.publish('homeassistant/status', 'online')
        self.broker.wait(self.discovery)
        self.broker.wait(f'{self.root}/availability', b'online')
        self.broker.connection.shutdown(socket.SHUT_RDWR)
        self.broker.wait(f'{self.root}/availability', b'online')
        self.assertTrue(self.client.network_status()['mqtt_connected'])
        self.client.close()
        self.client = Client(ProcessTransport(self.receiver, self.storage))
        self.assertEqual(self.client.describe()['device_id'], self.device_id)
        self.broker.wait(f'{self.root}/availability', b'online')
        self.broker.publish(f'{self.root}/desk/set', 'ON')
        self.broker.wait(f'{self.root}/desk/state', b'ON')
        del self.config['devices'][0]['home_assistant']
        self.client.configure(self.config)
        self.broker.wait(self.discovery, b'')
        self.broker.wait(f'{self.root}/desk/state', b'')
        self.broker.publish(f'{self.root}/desk/set', 'OFF')
        time.sleep(.05)
        self.assertTrue(self.client.scene('desk')['playing'])

    def test_validation_and_secret_preservation(self):
        for field, value in [('host', 'broker.local'), ('port', 0), ('enabled', 'yes'), ('extra', 1)]:
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
        config['devices'][0]['home_assistant']['on_preset'] = 'bad/preset'
        with self.assertRaises(ProtocolError):
            self.client.configure(config)


if __name__ == '__main__':
    unittest.main()
