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
        suffix = '_press_feedback' if kind == 'switch' else ''
        return f'homeassistant/{kind}/{self.controller}/i_{b"keypad".hex()}_{n}{suffix}/config'

    def wait_ready(self):
        self.broker.wait(f'{self.root}/availability', b'online')
        self.broker.wait(self.key(16) + '/press_feedback/state')

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

    def frame(self):
        self.client.pixel_snapshot.clear()
        self.client.manage('indicator_snapshot', output='key_lights')
        return bytes(self.client.pixel_snapshot)

    def wait_pixel(self, rgb, pixel=0):
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            actual = self.frame()[pixel*3:pixel*3+3]
            if actual == bytes(rgb):
                return
            time.sleep(.01)
        self.fail(f'pixel {pixel}: expected {rgb}, got {list(actual)}')

    def feedback(self, key, enabled):
        value = b'ON' if enabled else b'OFF'
        self.broker.publish(self.key(key) + '/press_feedback/set', value)
        self.broker.wait(self.key(key) + '/press_feedback/state', value)

    def event(self, n, event_type, count=None):
        payload = {'event_type': event_type}
        if count is not None:
            payload['multi_press_count'] = count
        return self.broker.wait(self.key(n) + '/event', json.dumps(payload, separators=(',', ':')).encode())

    def test_one_device_with_sixteen_lights_switches_and_button_event_entities(self):
        ids = []
        for n in range(1, 17):
            light = json.loads(self.broker.latest[self.discovery(n)])
            self.assertEqual(light['supported_color_modes'], ['rgb'])
            self.assertTrue(light['brightness'])
            self.assertFalse(light['retain'])
            self.assertFalse(light['transition'])
            self.assertFalse(light['flash'])
            self.assertTrue(light['effect'])
            self.assertEqual(light['effect_list'], ['steady', 'slow_blink', 'rapid_blink', '010100'])
            switch = json.loads(self.broker.latest[self.discovery(n, 'switch')])
            self.assertEqual(switch['device'], light['device'])
            self.assertEqual(switch['entity_category'], 'config')
            self.assertEqual(switch['command_topic'], self.key(n) + '/press_feedback/set')
            self.assertEqual(switch['state_topic'], self.key(n) + '/press_feedback/state')
            self.assertFalse(switch['retain'])
            self.assertEqual(switch['availability_mode'], 'all')
            self.assertEqual(switch['availability'], [{'topic': self.root + '/availability'},
                                                     {'topic': self.key(n) + '/availability'},
                                                     {'topic': self.key(n) + '/event/availability'}])
            self.assertEqual(self.broker.latest[switch['state_topic']], b'OFF')
            event = json.loads(self.broker.latest[self.discovery(n, 'event')])
            self.assertEqual(event['device'], light['device'])
            self.assertEqual(event['device']['identifiers'], [self.controller])
            self.assertEqual(event['device_class'], 'button')
            self.assertEqual(event['state_topic'], self.key(n) + '/event')
            self.assertEqual(event['event_types'], ['press_start', 'press_end', 'long_press_start',
                                                  'long_press_end', 'multi_press_ongoing', 'multi_press_end'])
            self.assertEqual(event['availability_mode'], 'all')
            self.assertEqual(event['availability'], [{'topic': self.root + '/availability'},
                                                    {'topic': self.key(n) + '/event/availability'}])
            ids += [light['unique_id'], event['unique_id'], switch['unique_id']]
        self.assertEqual(len(set(ids)), 48)
        subscriptions = []
        while not self.broker.subscriptions.empty():
            subscriptions.append(self.broker.subscriptions.get_nowait())
        self.assertIn(self.root + '/input/+/+/press_feedback/set', subscriptions)
        self.assertFalse(any(t.startswith('homeassistant/device_automation/') for t in self.broker.latest))
        self.masks(65535)
        self.event(16, 'press_start')
        events = [v for v in self.broker.history if v[0].endswith('/event')]
        self.assertEqual([v[0] for v in events], [self.key(n) + '/event' for n in range(1, 17)])
        self.assertTrue(all(not retained for _, _, retained in events))
        self.masks(0)
        self.event(16, 'press_end')
        self.event(16, 'multi_press_end', 1)
        self.assertTrue(self.client.describe()['inputs'][0]['available'])

    def test_double_press_and_hold_have_standard_nonretained_event_sequences(self):
        before = len(self.broker.history)
        for count in (1, 2):
            self.masks(1)
            self.event(1, 'press_start')
            if count == 2:
                self.event(1, 'multi_press_ongoing', 2)
            self.masks(0)
            self.event(1, 'press_end')
        self.event(1, 'multi_press_end', 2)
        events = [(json.loads(payload), retained) for topic, payload, retained in self.broker.history[before:]
                  if topic.endswith('/event')]
        self.assertEqual([e['event_type'] for e, _ in events],
                         ['press_start', 'press_end', 'press_start', 'multi_press_ongoing', 'press_end', 'multi_press_end'])
        self.assertTrue(all(not retained for _, retained in events))
        before = len(self.broker.history)
        self.masks(1)
        self.event(1, 'press_start')
        self.event(1, 'long_press_start')
        time.sleep(.1)
        self.masks(0)
        self.event(1, 'long_press_end')
        self.drain(.35)
        events = [json.loads(p)['event_type'] for t, p, _ in self.broker.history[before:] if t.endswith('/event')]
        self.assertEqual(events, ['press_start', 'long_press_start', 'long_press_end'])

    def test_rgb_commands_are_independent_atomic_and_use_normal_application_boundary(self):
        command = {'state': 'ON', 'brightness': 128, 'color': {'r': 255, 'g': 80, 'b': 20}}
        self.broker.publish(self.key(1) + '/set', json.dumps(command))
        state = json.loads(self.broker.wait(self.key(1) + '/state'))
        self.assertEqual(state, {**command, 'color_mode': 'rgb', 'effect': 'steady'})
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
        self.feedback(1, True)
        self.masks(1)
        self.event(1, 'press_start')
        self.broker.publish(self.key(1) + '/set', '{"state":"ON","brightness":32,"color":{"r":10,"g":20,"b":30}}')
        state = self.broker.wait(self.key(1) + '/state')
        before = len(self.broker.history)
        self.broker.publish('homeassistant/status', 'online')
        self.wait_ready()
        self.assertEqual(self.broker.latest[self.key(1) + '/state'], state)
        self.wait_pixel([10, 20, 30])
        self.assertFalse(any(t.endswith('/event') for t, _, _ in self.broker.history[before:]))
        self.broker.connection.shutdown(2)
        self.wait_ready()
        self.assertEqual(self.broker.latest[self.key(1) + '/state'], state)
        self.wait_pixel([10, 20, 30])
        self.client.close()
        self.start()
        self.wait_ready()
        self.assertEqual(self.indicator()['state'], 'OFF')
        self.wait_pixel([255, 255, 255])
        self.drain(.6)
        self.masks(0)
        self.wait_pixel([0, 0, 0])
        self.drain(.35)
        events = [v for v in self.broker.history if v[0].endswith('/event')]
        self.assertEqual(len(events), 1)

    def test_missing_expander_recovers_without_synthetic_press_and_removal_cleans_discovery(self):
        self.raw.unlink()
        time.sleep(.03)
        self.assertFalse(self.client.describe()['inputs'][0]['available'])
        self.broker.wait(self.key(16) + '/event/availability', b'offline')
        self.assertEqual(self.broker.latest[self.key(16) + '/availability'], b'online')
        self.masks(1)
        time.sleep(1.05)
        self.assertTrue(self.client.describe()['inputs'][0]['available'])
        self.broker.wait(self.key(16) + '/event/availability', b'online')
        self.drain()
        self.assertFalse(any(t.endswith('/event') for t, _, _ in self.broker.history))
        self.config['inputs'] = []
        self.client.configure(self.config)
        self.broker.wait(self.discovery(16), b'')
        self.broker.wait(self.key(16) + '/event/availability', b'')
        for n in range(1, 17):
            for kind in ('light', 'event', 'switch'):
                self.assertEqual(self.broker.latest[self.discovery(n, kind)], b'')
            self.assertEqual(self.broker.latest[self.key(n) + '/press_feedback/state'], b'')

    def test_configuration_rejects_pin_bus_and_indicator_conflicts_before_apply(self):
        for mutate in (
            lambda c: c['outputs'][0]['config'].update(clock_pin='GP19'),
            lambda c: c['outputs'][0]['config'].update(data_pin='GP4'),
            lambda c: c['inputs'][0]['config'].update(scl='GP7'),
            lambda c: c['inputs'][0]['config'].update(address=40),
            lambda c: c['inputs'][0]['config'].update(long_press_ms=99),
            lambda c: c['inputs'][0]['config'].update(long_press_ms=5001),
            lambda c: c['inputs'][0]['config'].update(multi_press_ms=99),
            lambda c: c['inputs'][0]['config'].update(multi_press_ms=1001),
            lambda c: c['inputs'][0]['home_assistant'].update(indicators='missing'),
            lambda c: c['outputs'][0].update(startup={'mode': 'restore_last'}),
        ):
            candidate = copy.deepcopy(self.config)
            mutate(candidate)
            with self.assertRaises(ProtocolError):
                self.client.configure(candidate)
            self.client.manage('config_get')
            self.assertEqual(json.loads(self.client.data_reply), self.config)

    def test_label_edits_preserve_rgb_identity_and_removing_indicator_binding_keeps_events(self):
        initial = json.loads(self.broker.latest[self.discovery(1)])
        initial_event = json.loads(self.broker.latest[self.discovery(1, 'event')])
        initial_switch = json.loads(self.broker.latest[self.discovery(1, 'switch')])
        self.feedback(1, True)
        self.broker.publish(self.key(1) + '/set', '{"state":"ON"}')
        self.broker.wait(self.key(1) + '/state')
        self.config['inputs'][0]['home_assistant']['names'] = ['Kitchen "lamp" 🌈'] + [f'Key {n}' for n in range(2, 17)]
        self.client.configure(self.config)
        updated = json.loads(self.broker.wait(self.discovery(1)))
        self.assertEqual(updated['name'], 'Kitchen "lamp" 🌈 indicator')
        self.assertEqual(updated['unique_id'], initial['unique_id'])
        updated_event = json.loads(self.broker.latest[self.discovery(1, 'event')])
        self.assertEqual(updated_event['name'], 'Kitchen "lamp" 🌈')
        self.assertEqual(updated_event['unique_id'], initial_event['unique_id'])
        self.assertEqual(self.indicator()['state'], 'ON')
        self.broker.wait(self.key(1) + '/press_feedback/state', b'ON')
        updated_switch = json.loads(self.broker.latest[self.discovery(1, 'switch')])
        self.assertEqual(updated_switch['unique_id'], initial_switch['unique_id'])
        self.assertEqual(updated_switch['name'], 'Kitchen "lamp" 🌈 press feedback')
        del self.config['inputs'][0]['home_assistant']['indicators']
        self.client.configure(self.config)
        self.broker.wait(self.discovery(16), b'')
        # Wait through the retained removal to the re-created event-only binding.
        while not self.broker.wait(self.discovery(16, 'event')):
            pass
        self.masks(1)
        self.event(1, 'press_start')
        self.assertEqual(self.broker.latest[self.discovery(1)], b'')
        self.assertEqual(self.broker.latest[self.discovery(1, 'switch')], b'')

    def test_effects_accept_colour_and_brightness_and_reject_invalid_updates_atomically(self):
        for effect in ('steady', 'slow_blink', 'rapid_blink', '010100'):
            value = {'state': 'ON', 'brightness': 64, 'color': {'r': 255, 'g': 0, 'b': 0}, 'effect': effect}
            self.broker.publish(self.key(1) + '/set', json.dumps(value))
            state = json.loads(self.broker.wait(self.key(1) + '/state'))
            self.assertEqual(state, {**value, 'color_mode': 'rgb'})
            self.wait_pixel([64, 0, 0])
            if effect != 'steady':
                self.wait_pixel([0, 0, 0])
            before = len(self.broker.history)
            self.drain(.2)
            self.assertFalse(any(t == self.key(1) + '/state' for t, _, _ in self.broker.history[before:]))
        for effect in ('unknown', 10100, {'speed': 2}, None):
            self.broker.publish(self.key(1) + '/set', json.dumps({'state': 'OFF', 'brightness': 255, 'effect': effect}))
            self.drain(.05)
            self.assertEqual(self.indicator(), state)
        self.broker.publish(self.key(1) + '/set', '{"effect":"steady"}', retained=True)
        self.drain(.05)
        self.assertEqual(self.indicator(), state)

    def test_feedback_persists_per_key_and_ignores_retained_or_malformed_commands(self):
        self.feedback(1, True)
        self.feedback(16, True)
        saved = (self.path / 'store.bin').read_bytes()
        for payload, retained in [(b'ON', False), (b'OFF', True), (b'true', False), (b'ON\x00', False)]:
            self.broker.publish(self.key(1) + '/press_feedback/set', payload, retained=retained)
        self.drain()
        self.assertEqual((self.path / 'store.bin').read_bytes(), saved)
        self.client.close()
        self.start()
        self.wait_ready()
        for n in range(1, 17):
            self.assertEqual(self.broker.latest[self.key(n) + '/press_feedback/state'], b'ON' if n in (1, 16) else b'OFF')
        self.feedback(1, False)
        self.client.close()
        self.start()
        self.wait_ready()
        self.assertEqual(self.broker.latest[self.key(1) + '/press_feedback/state'], b'OFF')
        self.assertEqual(self.broker.latest[self.key(16) + '/press_feedback/state'], b'ON')
        self.masks(1 << 15)
        self.wait_pixel([255, 255, 255], pixel=15)
        self.wait_pixel([0, 0, 0], pixel=0)
        self.masks(0)
        self.wait_pixel([0, 0, 0], pixel=15)

    def test_feedback_overrides_blink_and_off_without_changing_ha_target(self):
        self.feedback(1, True)
        value = {'state': 'ON', 'brightness': 32, 'color': {'r': 255, 'g': 0, 'b': 0}, 'effect': 'rapid_blink'}
        self.broker.publish(self.key(1) + '/set', json.dumps(value))
        state = json.loads(self.broker.wait(self.key(1) + '/state'))
        self.wait_pixel([0, 0, 0])
        self.masks(3)
        self.event(2, 'press_start')
        self.wait_pixel([255, 0, 0])
        self.wait_pixel([0, 0, 0], pixel=1)
        self.drain(.3)
        self.assertEqual(self.indicator(), state)
        self.wait_pixel([255, 0, 0])
        # Changing the HA target while held updates its colour, without losing the override.
        self.broker.publish(self.key(1) + '/set', '{"state":"OFF","color":{"r":0,"g":255,"b":0}}')
        state = json.loads(self.broker.wait(self.key(1) + '/state'))
        self.wait_pixel([0, 255, 0])
        self.assertEqual(state['brightness'], 32)
        self.assertEqual(state['effect'], 'rapid_blink')
        self.masks(0)
        self.wait_pixel([0, 0, 0])
        self.assertEqual(self.indicator(), state)

    def test_feedback_works_without_mqtt_and_clears_when_input_is_unavailable(self):
        self.feedback(1, True)
        self.client.configure_network({'schema': 'lightvm.network', 'wifi': {'enabled': False, 'networks': []},
                                       'http': {'enabled': False}, 'mqtt': {'enabled': False}})
        self.client.manage('indicator_set', output='key_lights', pixel=0,
                           value={'state': 'ON', 'brightness': 20, 'color': {'r': 255, 'g': 0, 'b': 0}})
        self.wait_pixel([20, 0, 0])
        self.masks(1)
        self.wait_pixel([255, 0, 0])
        self.raw.unlink()
        self.wait_pixel([20, 0, 0])
        self.assertFalse(self.client.describe()['inputs'][0]['available'])
        self.masks(0)
        time.sleep(1.05)
        self.masks(1)
        self.wait_pixel([255, 0, 0])
        self.masks(0)
        self.wait_pixel([20, 0, 0])

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
        self.event(1, 'press_start')
        self.broker.publish(self.key(1) + '/set', '{"state":"ON","color":{"r":1,"g":2,"b":3}}')
        self.assertEqual(json.loads(self.broker.wait(self.key(1) + '/state'))['color'], {'r': 1, 'g': 2, 'b': 3})
        self.client.publish_inputs(publisher, 2, {'remote.level': .25})
        self.client.require_ok(self.client.command(FRAME, struct.pack('<I', 0), output='desk'))
        self.assertEqual(bytes(self.client.pixel_snapshot[:3]), bytes([25, 0, 0]))
        self.assertEqual(self.client.program_state(output='desk')['generation'], generation)
        self.assertTrue(self.client.describe()['inputs'][0]['available'])


if __name__ == '__main__':
    unittest.main()
