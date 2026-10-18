"""Native TCA9555 sampling + real MQTT wire + independent RGB indicator control."""
import copy
import json
import os
from pathlib import Path
import queue
import struct
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from client import Client, ProcessTransport, ProtocolError, FRAME
from lightvm.compiler import compile_source
from test_mqtt import Broker


class KeypadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.receiver = Path(os.environ.get('LIGHTVM_RECEIVER', ROOT / 'build/keypad-host/lightvm'))
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
        self.path = Path(self.directory.name)
        self.raw = self.path / 'inputs.bin'
        self.raw.write_bytes(b'\xff' * 16)
        self.broker = Broker()
        self.config = json.loads((ROOT / 'examples/config/pico-rgb-keypad.json').read_text())
        self.start()
        self.client.configure(self.config, save=True)
        self.client.configure_network({'schema': 'lightvm.network', 'wifi': {'enabled': False, 'networks': []},
                                       'http': {'enabled': False}, 'mqtt': {'enabled': True, 'host': 'localhost', 'port': self.broker.port}})
        self.controller = self.client.describe()['controller_id']
        self.root = f'homeassistant/lightvm/{self.controller}'
        self.wait_ready()

    def start(self):
        with patch.dict(os.environ, {'LIGHTVM_I2C_INPUTS': str(self.raw)}):
            self.client = Client(ProcessTransport(self.receiver, self.path / 'store.bin'))

    def key(self, n):
        return f'{self.root}/input/keypad/{n}'

    def discovery(self, n, kind='light'):
        component = 'light' if kind == 'light' else 'device_automation'
        suffix = '' if kind == 'light' else '_' + kind
        return f'homeassistant/{component}/{self.controller}/i_{b"keypad".hex()}_{n}{suffix}/config'

    def wait_ready(self):
        self.broker.wait(f'{self.root}/availability', b'online')
        self.broker.wait(self.key(16) + '/state')

    def tearDown(self):
        self.client.close()
        self.broker.close()
        self.assertFalse(self.broker.errors)
        self.directory.cleanup()

    def masks(self, pressed):
        temporary = self.raw.with_suffix('.new')
        temporary.write_bytes((65535 ^ pressed).to_bytes(2, 'little') + b'\xff' * 14)
        temporary.replace(self.raw)

    def drain(self, seconds=.1):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            try:
                item = self.broker.messages.get(timeout=.01)
                self.broker.history.append(item)
                self.broker.latest[item[0]] = item[1]
            except queue.Empty:
                pass

    def indicator(self, pixel=0):
        self.client.manage('indicator_get', output='key_lights', pixel=pixel)
        return json.loads(self.client.data_reply)

    def test_one_device_with_sixteen_lights_and_distinct_press_release_triggers(self):
        ids = []
        for n in range(1, 17):
            light = json.loads(self.broker.latest[self.discovery(n)])
            self.assertEqual(light['supported_color_modes'], ['rgb'])
            self.assertTrue(light['brightness'])
            self.assertFalse(light['retain'])
            self.assertFalse(light['transition'])
            self.assertFalse(light['flash'])
            ids.append(light['unique_id'])
            for kind in ('press', 'release'):
                trigger = json.loads(self.broker.latest[self.discovery(n, kind)])
                self.assertEqual(trigger['device'], light['device'])
                self.assertEqual(trigger['device']['identifiers'], [self.controller])
                self.assertEqual(trigger['subtype'], f'keypad_key_{n}')
                self.assertEqual(trigger['payload'], kind)
        self.assertEqual(len(set(ids)), 16)
        self.masks(65535)
        self.broker.wait(self.key(16) + '/event', b'press')
        events = [v for v in self.broker.history if v[0].endswith('/event')]
        self.assertEqual([v[0] for v in events], [self.key(n) + '/event' for n in range(1, 17)])
        self.assertTrue(all(not retained for _, _, retained in events))
        self.masks(0)
        self.broker.wait(self.key(16) + '/event', b'release')
        self.assertTrue(self.client.describe()['inputs'][0]['available'])

    def test_rgb_commands_are_independent_atomic_and_use_normal_application_boundary(self):
        command = {'state': 'ON', 'brightness': 128, 'color': {'r': 255, 'g': 80, 'b': 20}}
        self.broker.publish(self.key(1) + '/set', json.dumps(command))
        state = json.loads(self.broker.wait(self.key(1) + '/state'))
        self.assertEqual(state, {**command, 'color_mode': 'rgb'})
        self.assertEqual(self.indicator(), state)
        self.assertEqual(self.indicator(1)['state'], 'OFF')
        self.client.manage('indicator_snapshot', output='key_lights')
        self.assertEqual(bytes(self.client.pixel_snapshot), bytes([128, 40, 10]) + bytes(45))
        self.broker.publish(self.key(1) + '/set', '{"state":"OFF"}', retained=True)
        self.broker.publish(self.key(1) + '/set', '{"state":"OFF","color":{"r":256,"g":1,"b":1}}')
        self.drain()
        self.assertEqual(self.indicator(), state)
        self.client.manage('indicator_set', output='key_lights', pixel=15, value={'state': 'ON', 'color': {'r': 1, 'g': 2, 'b': 3}})
        other = json.loads(self.broker.wait(self.key(16) + '/state'))
        self.assertEqual(other['color'], {'r': 1, 'g': 2, 'b': 3})
        for op in ('program_activate', 'composition_clear', 'output_power', 'transition_set'):
            with self.assertRaises(ProtocolError):
                self.client.manage(op, output='key_lights')

    def test_birth_reconnect_and_reboot_do_not_replay_held_keys(self):
        self.masks(1)
        self.broker.wait(self.key(1) + '/event', b'press')
        self.broker.publish(self.key(1) + '/set', '{"state":"ON","color":{"r":10,"g":20,"b":30}}')
        state = self.broker.wait(self.key(1) + '/state')
        before = len(self.broker.history)
        self.broker.publish('homeassistant/status', 'online')
        self.wait_ready()
        self.assertEqual(self.broker.latest[self.key(1) + '/state'], state)
        self.assertFalse(any(t.endswith('/event') for t, _, _ in self.broker.history[before:]))
        self.broker.connection.shutdown(2)
        self.wait_ready()
        self.assertEqual(self.broker.latest[self.key(1) + '/state'], state)
        self.client.close()
        self.start()
        self.wait_ready()
        self.assertEqual(self.indicator()['state'], 'OFF')
        self.drain()
        events = [v for v in self.broker.history if v[0].endswith('/event')]
        self.assertEqual(len(events), 1)

    def test_missing_expander_recovers_without_synthetic_press_and_removal_cleans_discovery(self):
        self.raw.unlink()
        time.sleep(.03)
        self.assertFalse(self.client.describe()['inputs'][0]['available'])
        self.masks(1)
        time.sleep(1.05)
        self.assertTrue(self.client.describe()['inputs'][0]['available'])
        self.drain()
        self.assertFalse(any(t.endswith('/event') for t, _, _ in self.broker.history))
        self.config['inputs'] = []
        self.client.configure(self.config)
        self.broker.wait(self.discovery(16), b'')
        self.broker.wait(self.key(16) + '/availability', b'')
        for n in range(1, 17):
            for kind in ('light', 'press', 'release'):
                self.assertEqual(self.broker.latest[self.discovery(n, kind)], b'')

    def test_configuration_rejects_pin_bus_and_indicator_conflicts_before_apply(self):
        for mutate in (
            lambda c: c['outputs'][0]['config'].update(clock_pin='GP19'),
            lambda c: c['outputs'][0]['config'].update(data_pin='GP4'),
            lambda c: c['inputs'][0]['config'].update(scl='GP7'),
            lambda c: c['inputs'][0]['config'].update(address=40),
            lambda c: c['inputs'][0]['home_assistant'].update(indicators='missing'),
            lambda c: c['outputs'][0].update(startup={'program': 'nope'}),
        ):
            candidate = copy.deepcopy(self.config)
            mutate(candidate)
            with self.assertRaises(ProtocolError):
                self.client.configure(candidate)
            self.client.manage('config_get')
            self.assertEqual(json.loads(self.client.data_reply), self.config)

    def test_label_edits_preserve_rgb_identity_and_removing_indicator_binding_keeps_triggers(self):
        initial = json.loads(self.broker.latest[self.discovery(1)])
        self.broker.publish(self.key(1) + '/set', '{"state":"ON"}')
        self.broker.wait(self.key(1) + '/state')
        self.config['inputs'][0]['home_assistant']['names'] = ['Kitchen "lamp" 🌈'] + [f'Key {n}' for n in range(2, 17)]
        self.client.configure(self.config)
        updated = json.loads(self.broker.wait(self.discovery(1)))
        self.assertEqual(updated['name'], 'Kitchen "lamp" 🌈 indicator')
        self.assertEqual(updated['unique_id'], initial['unique_id'])
        self.assertEqual(self.indicator()['state'], 'ON')
        self.broker.wait(self.key(1) + '/state')
        del self.config['inputs'][0]['home_assistant']['indicators']
        self.client.configure(self.config)
        self.broker.wait(self.discovery(16), b'')
        # Wait through the retained removal to the re-created press-only binding.
        while not self.broker.wait(self.discovery(16, 'release')):
            pass
        self.masks(1)
        self.broker.wait(self.key(1) + '/event', b'press')
        self.assertEqual(self.broker.latest[self.discovery(1)], b'')

    def test_live_animation_sources_coexist_with_hardware_inputs_and_rgb_indicators(self):
        self.config['outputs'].append({'id': 'desk', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': 8}})
        self.client.configure(self.config)
        publisher = self.client.claim_inputs({'remote.level': 'float'})
        self.client.publish_inputs(publisher, 1, {'remote.level': .75})
        compiled = compile_source('''from lightvm import profile,render,Input,rgb
profile("animation")
LEVEL=Input("level",default=0.0,minimum=0.0,maximum=1.0)
@render
def draw(ctx,runtime,pixels):
 pixels.fill(rgb(int(runtime.input(LEVEL)*100.0),0,0))
''')
        self.assertTrue(compiled.success, compiled.diagnostics)
        self.client.upload(compiled.bytecode, output='desk', inputs={'level': {'source': 'remote.level', 'timeout_ms': 5000}})
        self.client.require_ok(self.client.command(FRAME, struct.pack('<I', 0), output='desk'))
        self.assertEqual(bytes(self.client.pixel_snapshot[:3]), bytes([75, 0, 0]))
        generation = self.client.program_state(output='desk')['generation']
        self.masks(1)
        self.broker.wait(self.key(1) + '/event', b'press')
        self.broker.publish(self.key(1) + '/set', '{"state":"ON","color":{"r":1,"g":2,"b":3}}')
        self.assertEqual(json.loads(self.broker.wait(self.key(1) + '/state'))['color'], {'r': 1, 'g': 2, 'b': 3})
        self.client.publish_inputs(publisher, 2, {'remote.level': .25})
        self.client.require_ok(self.client.command(FRAME, struct.pack('<I', 0), output='desk'))
        self.assertEqual(bytes(self.client.pixel_snapshot[:3]), bytes([25, 0, 0]))
        self.assertEqual(self.client.program_state(output='desk')['generation'], generation)
        self.assertTrue(self.client.describe()['inputs'][0]['available'])


if __name__ == '__main__':
    unittest.main()
