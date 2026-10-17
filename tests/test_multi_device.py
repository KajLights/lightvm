"""Independent devices share one upload workspace and cooperative scheduler."""
import copy
import json
import os
from pathlib import Path
import struct
import sys
import time
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from lightvm.compiler import compile_source
from client import Client, ProcessTransport, ProtocolError, BEGIN, CHUNK, FINISH, FRAME, PLAY, RESET


def configuration(number=6, count=17):
    return {'schema': 'lightvm.config', 'devices': [
        {'id': f'lights{i}', 'type': 'ws2812b', 'config': {'pin': f'GP{i}', 'count': count}}
        for i in range(number)]}


class MultiDeviceTests(unittest.TestCase):
    def setUp(self):
        self.client = Client(ProcessTransport(os.environ.get('LIGHTVM_RECEIVER', ROOT / 'build/host/lightvm')))
        self.client.configure(configuration())
        self.effects = {}
        for name in ('rainbow', 'color', 'pattern', 'rainbow_sprite', 'sweep', 'stars'):
            result = compile_source((ROOT / 'examples/animations' / f'{name}.py').read_text())
            self.assertTrue(result.success, result.diagnostics)
            self.effects[name] = result.bytecode

    def tearDown(self):
        self.client.close()

    def load(self, name, slot, play=True):
        self.client.upload(self.effects[name], device=slot)
        self.client.require_ok(self.client.command(PLAY if play else FRAME, b'' if play else bytes(4), device=slot))

    def test_six_instances_and_independent_controls(self):
        for slot, name in zip([f'lights{i}' for i in range(6)], self.effects):
            self.load(name, slot, play=False)
            state = self.client.program_state(device=slot)
            self.assertEqual(state['slot'], 'A')
            self.assertEqual(state['device'], slot)
            self.assertEqual(len(self.client.snapshot(device=slot)[1]), 51)
            self.assertEqual(self.client.status_fields(device=slot)['code_storage'], 'flash')
        self.client.reset_program(settings={'duration': 17.0}, device='lights4')
        for slot in [f'lights{i}' for i in range(6)]:
            self.client.require_ok(self.client.command(PLAY, device=slot))
        start = {slot: self.client.program_state(device=slot) for slot in [f'lights{i}' for i in range(6)]}
        heap = self.client.status_fields()['heap']
        time.sleep(.12)
        for slot in [f'lights{i}' for i in range(6)]:
            state = self.client.program_state(device=slot)
            self.assertGreater(state['output_frame'], start[slot]['output_frame'])
            self.assertFalse(state['faulted'])
        self.assertEqual(self.client.status_fields()['heap'], heap)
        self.client.pause_program(device='lights1')
        paused = self.client.program_state(device='lights1')['program_us']
        running = self.client.program_state(device='lights0')['program_us']
        time.sleep(.06)
        self.assertEqual(self.client.program_state(device='lights1')['program_us'], paused)
        self.assertGreater(self.client.program_state(device='lights0')['program_us'], running)
        self.client.resume_program(device='lights1')
        time.sleep(.04)
        self.assertGreater(self.client.program_state(device='lights1')['program_us'], paused)
        a_generation = self.client.program_state()['generation']
        self.client.reset_program(settings={'random_color': False, 'color': 0x123456}, device='lights1')
        self.client.require_ok(self.client.command(FRAME, bytes(4), device='lights1'))
        self.assertEqual(bytes(self.client.pixel_snapshot), bytes.fromhex('123456') * 17)
        self.assertEqual(self.client.program_state()['generation'], a_generation)
        for slot in [f'lights{i}' for i in range(6)]:
            self.client.require_ok(self.client.command(RESET, device=slot))
        self.assertEqual(self.client.status_fields()['heap'], '0')

    def test_adding_and_removing_devices_preserves_existing_playback(self):
        self.client.configure(configuration(1))
        self.load('rainbow', 'lights0')
        before = self.client.program_state()
        heap = self.client.status_fields()['heap']
        self.client.configure(configuration(6))
        self.assertEqual(self.client.program_state()['generation'], before['generation'])
        self.assertEqual(self.client.status_fields()['mode'], 'bytecode')
        self.load('stars', 'lights5')
        time.sleep(.1)
        self.client.configure(configuration(1))
        after = self.client.program_state()
        self.assertEqual(after['generation'], before['generation'])
        self.assertGreater(after['program_us'], before['program_us'])
        self.assertEqual(self.client.status_fields()['heap'], heap)
        with self.assertRaises(ProtocolError):
            self.client.status_fields(device='lights5')

    def test_shared_upload_keeps_destination_and_running_instances(self):
        self.load('rainbow', 'lights0')
        self.load('color', 'lights1')
        before = self.client.program_state()
        data = self.effects['stars']
        self.client.require_ok(self.client.command(BEGIN, struct.pack('<II', len(data), zlib.crc32(data)) + b'lights2'))
        self.client.reset_program(settings={'random_color': False}, device='lights1')
        for offset in range(0, len(data), 508):
            self.client.require_ok(self.client.command(CHUNK, struct.pack('<I', offset) + data[offset:offset+508]))
            time.sleep(.004)
        self.client.require_ok(self.client.command(FINISH))
        self.client.require_ok(self.client.command(PLAY, device='lights2'))
        self.assertEqual(self.client.program_state(device='lights2')['slot'], 'A')
        self.assertEqual(self.client.program_state()['generation'], before['generation'])
        self.assertGreater(self.client.program_state()['output_frame'], before['output_frame'])
        generation = self.client.program_state(device='lights2')['generation']
        with self.assertRaises(ProtocolError):
            self.client.upload(b'LBC1' + bytes(20), device='lights2')
        self.assertEqual(self.client.program_state(device='lights2')['generation'], generation)
        self.assertEqual(self.client.status_fields()['mode'], 'bytecode')

    def test_more_than_eight_devices_and_no_aggregate_pixel_ceiling(self):
        capacity = self.client.describe()['pixel_capacity']
        count = min(capacity, max(200, capacity // 9 + 1))
        # Each output obeys its board limit; only the aggregate exceeds it.
        self.assertGreater(9 * count, capacity)
        config = configuration(9, count)
        self.client.configure(config)
        self.assertEqual(len(self.client.describe()['devices']), 9)
        self.load('color', 'lights8')
        self.assertEqual(len(self.client.snapshot(device='lights8')[1]), count * 3)
        # Reordering device declarations must preserve identity and generation.
        before = self.client.program_state(device='lights8')
        config['devices'].reverse()
        self.client.configure(config)
        self.assertEqual(self.client.program_state(device='lights8')['generation'], before['generation'])
        self.assertEqual(self.client.status_fields(device='lights8')['mode'], 'bytecode')

    def test_configuration_rejects_collisions_and_global_slots(self):
        valid = configuration()
        candidates = []
        duplicate_id = copy.deepcopy(valid)
        duplicate_id['devices'][1]['id'] = 'lights0'
        candidates.append(duplicate_id)
        wrong_slot = copy.deepcopy(valid)
        wrong_slot['targets'] = {'B': {'device': 'lights1'}}
        candidates.append(wrong_slot)
        pins = configuration(2)
        pins['devices'][1]['config']['pin'] = 'GP0'
        candidates.append(pins)
        virtual = configuration(1)
        virtual['devices'][0].update(type='virtual_rgb', config={'count': 8})
        candidates.append(virtual)
        for candidate in candidates:
            with self.assertRaises(ProtocolError):
                self.client.configure(candidate)
            self.assertEqual(self.client.get_config(), valid)
        with self.assertRaises(ProtocolError):
            self.client.manage('program_state', device='lights0', slot='B')


if __name__ == '__main__':
    unittest.main()
