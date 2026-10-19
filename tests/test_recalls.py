"""Destination recalls and fixed A/B controls exercise the native application."""
import copy
import os
from pathlib import Path
import struct
import sys
import tempfile
import time
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from client import Client, ProcessTransport, ProtocolError, FRAME
from lightvm.compiler import compile_source


class RecallTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.storage = Path(self.directory.name) / 'store.bin'
        self.client = Client(ProcessTransport(os.environ.get('LIGHTVM_RECEIVER', ROOT / 'build/unified-animation-host/lightvm'), self.storage))
        self.config = {'schema': 'lightvm.config', 'outputs': [
            {'id': 'lights', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': 5}},
            {'id': 'other', 'type': 'ws2812b', 'config': {'pin': 'GP17', 'count': 5}}]}
        self.client.configure(self.config, save=True)
        source = (ROOT / 'examples/animations/color.py').read_text()
        self.brush = compile_source(source).bytecode
        self.client.save_program('brush', self.brush)
        for name in ('crossfade', 'wipe'):
            program = compile_source((ROOT / f'examples/transitions/{name}.py').read_text()).bytecode
            self.client.save_program(name, program)
        self.programs = {p['name']: p for p in self.client.programs(program_type='transition')}
        for name, color in [('red', 0xff0000), ('blue', 0x0000ff), ('green', 0x00ff00)]:
            layers = [{'id': 'base', 'program': 'brush', 'crc32': zlib.crc32(self.brush), 'settings': {'color': color, 'random_color': False}}]
            self.client.save_preset(name, layers)

    def tearDown(self):
        self.client.close()
        self.directory.cleanup()

    def spec(self, name='wipe', duration=100000, **settings):
        return {'program': name, 'crc32': self.programs[name]['crc32'], 'duration_ms': duration, 'settings': settings}

    def save_entry(self, name, spec):
        self.client.save_preset(name, self.client.preset(name)['layers'], entry_transition=spec)

    def state(self):
        return self.client.transition('lights')

    def frame(self):
        self.client.require_ok(self.client.command(FRAME, struct.pack('<I', 0), output='lights'))
        return bytes(self.client.pixel_snapshot)

    def test_recall_completes_previous_target_then_runs_forward_without_restarting_source(self):
        self.client.recall_preset('red', 'lights')
        self.save_entry('blue', self.spec())
        self.save_entry('green', self.spec('crossfade'))
        self.client.recall_preset('blue', 'lights')
        before = self.state()
        self.assertEqual((before['A']['preset'], before['B']['preset']), ('red', 'blue'))
        self.assertEqual((before['driver']['start'], before['driver']['target']), (0, 100))
        generation = before['B']['layers'][0]['generation']
        time.sleep(.02)
        self.client.recall_preset('green', 'lights')
        after = self.state()
        self.assertEqual((after['A']['preset'], after['B']['preset']), ('blue', 'green'))
        self.assertEqual(after['A']['layers'][0]['generation'], generation)
        self.assertEqual(after['driver']['start'], 0)
        self.assertEqual(after['program'], 'crossfade')
        self.assertEqual(after['recall'], {'power': True, 'preset': 'green'})
        self.assertEqual(self.client.program_state(output='lights', slot='A', layer='base')['slot'], 'A')
        self.assertEqual(self.frame(), bytes([0, 0, 255]) * 5)
        self.assertFalse(self.client.composition('other')['active'])
        revision = after['revision']
        self.client.recall_preset('green', 'lights')
        self.assertEqual(self.state()['revision'], revision)

    def test_first_recall_promotes_direct_playback_without_restarting_or_aliasing_generations(self):
        self.client.activate('brush', output='lights', settings={'color': 0xff0000, 'random_color': False})
        direct = self.client.program_state(output='lights')
        self.save_entry('blue', self.spec())
        self.client.recall_preset('blue', 'lights')
        state = self.state()
        outgoing = self.client.program_state(output='lights', slot='A', layer='main')
        self.assertEqual(outgoing['generation'], direct['generation'])
        self.assertEqual(outgoing['settings_values'], direct['settings_values'])
        self.assertGreater(state['B']['layers'][0]['generation'], outgoing['generation'])
        self.assertEqual(self.frame(), bytes([255, 0, 0]) * 5)

    def test_manual_endpoints_never_swap_and_explicit_swap_keeps_instances(self):
        self.client.activate_preset('red', 'lights', 'A')
        self.client.activate_preset('blue', 'lights', 'B')
        before = self.state()
        self.client.select_transition('wipe', output='lights')
        self.client.run_transition(100, 15, output='lights')
        time.sleep(.04)
        end = self.state()
        self.assertEqual(end['progress'], 100)
        self.assertEqual(end['A']['layers'][0]['generation'], before['A']['layers'][0]['generation'])
        self.assertEqual(end['B']['layers'][0]['generation'], before['B']['layers'][0]['generation'])
        self.client.run_transition(0, 0, output='lights')
        self.assertEqual(self.frame(), bytes([255, 0, 0]) * 5)
        self.client.set_progress(40, output='lights')
        self.assertEqual(self.frame(), bytes([0, 0, 255]) * 2 + bytes([255, 0, 0]) * 3)
        with self.assertRaises(ProtocolError):
            self.client.swap_transition('lights')
        self.client.set_progress(100, output='lights')
        token = self.client.assign_transition_source('fader', output='lights')['driver']['assignment']
        swapped = self.client.swap_transition('lights')
        self.assertEqual(swapped['progress'], 0)
        self.assertEqual(swapped['A']['layers'][0]['generation'], before['B']['layers'][0]['generation'])
        self.assertEqual(swapped['B']['layers'][0]['generation'], before['A']['layers'][0]['generation'])
        self.assertEqual(self.frame(), bytes([0, 0, 255]) * 5)
        self.assertFalse(self.client.update_transition_source('fader', token, 10, output='lights'))
        self.assertIsNone(swapped['recall'])

    def test_power_uses_the_preset_transition_remembers_selection_and_interrupts_to_black(self):
        self.save_entry('blue', self.spec('wipe'))
        self.client.set_power(True, 'lights', preset='blue')
        state = self.state()
        self.assertEqual(state['program'], 'wipe')
        self.assertFalse(state['A']['active'])
        self.assertEqual(state['B']['preset'], 'blue')
        token = state['driver']['assignment']
        self.client.set_power(False, 'lights')
        off = self.state()
        self.assertEqual(off['A']['preset'], 'blue')
        self.assertFalse(off['B']['active'])
        self.assertTrue(off['playing'])  # Rendering the fade is independent of logical power.
        self.assertEqual(off['program'], 'wipe')
        self.assertEqual(off['driver']['duration_ms'], self.spec()['duration_ms'])
        self.assertEqual(off['recall'], {'power': False, 'preset': 'blue'})
        description = self.client.describe()['outputs'][0]
        self.assertFalse(description['power'])
        self.assertEqual(description['selected_preset'], 'blue')
        self.assertGreater(off['driver']['assignment'], token)
        self.client.set_power(False, 'lights')
        self.assertEqual(self.state()['revision'], off['revision'])
        self.client.set_power(True, 'lights')
        on = self.state()
        self.assertFalse(on['A']['active'])  # The interrupted OFF was first completed to black.
        self.assertEqual(on['B']['preset'], 'blue')
        self.assertEqual(on['program'], 'wipe')
        self.assertEqual(self.frame(), bytes(15))
        self.assertEqual(self.client.get_config(saved=True), self.config)

    def test_preset_without_transition_cuts_on_and_off(self):
        self.client.set_power(True, 'lights', preset='blue')
        self.assertEqual(self.state()['progress'], 100)
        self.assertEqual(self.frame(), bytes([0, 0, 255]) * 5)
        self.client.set_power(False, 'lights')
        self.assertEqual(self.state()['progress'], 100)
        self.assertEqual(self.frame(), bytes(15))
        self.assertEqual(self.state()['recall'], {'power': False, 'preset': 'blue'})

    def test_entry_settings_are_applied_and_manual_takeover_retains_fixed_slots(self):
        self.client.recall_preset('red', 'lights')
        self.save_entry('blue', self.spec(reverse=True))
        self.client.recall_preset('blue', 'lights')
        token = self.client.assign_transition_source('fader', 'lights')['driver']['assignment']
        self.client.update_transition_source('fader', token, 40, 'lights')
        self.assertEqual(self.frame(), bytes([255, 0, 0]) * 3 + bytes([0, 0, 255]) * 2)
        before = self.state()
        self.client.update_transition_source('fader', token, 100, 'lights')
        self.client.update_transition_source('fader', token, 0, 'lights')
        after = self.state()
        self.assertEqual([after[slot]['layers'][0]['generation'] for slot in ('A', 'B')], [before[slot]['layers'][0]['generation'] for slot in ('A', 'B')])
        self.client.recall_preset('green', 'lights')
        self.assertFalse(self.client.update_transition_source('fader', token, 50, 'lights'))

    def test_invalid_transitions_and_stale_recalls_preserve_runtime_and_catalog(self):
        self.client.recall_preset('red', 'lights')
        before = self.state()
        catalog = self.client.describe()['catalog_revision']
        for patch in ({'crc32': 1}, {'duration_ms': -1}, {'program': 'missing'}, {'settings': {'unknown': True}}, {'settings': {'reverse': 1}}):
            with self.subTest(patch=patch), self.assertRaises(ProtocolError):
                self.save_entry('blue', {**self.spec(), **patch})
            self.assertEqual(self.state()['revision'], before['revision'])
            self.assertEqual(self.client.describe()['catalog_revision'], catalog)
        self.client.activate_preset('blue', 'lights', 'A')
        with self.assertRaises(ProtocolError):
            self.client.manage('preset_recall', name='green', output='lights', expected_revision=before['revision'])
        config = copy.deepcopy(self.config)
        for field in ('on_transition', 'off_transition'):
            config['outputs'][0][field] = self.spec()
            with self.assertRaises(ProtocolError):
                self.client.configure(config)
            del config['outputs'][0][field]
        self.assertEqual(self.client.get_config(), self.config)

    def test_transition_references_are_typed_pinned_and_persisted(self):
        spec = self.spec('wipe', 0)
        self.save_entry('blue', spec)
        self.save_entry('red', self.spec('crossfade', 0))
        self.client.close()
        self.client = Client(ProcessTransport(os.environ.get('LIGHTVM_RECEIVER', ROOT / 'build/unified-animation-host/lightvm'), self.storage))
        self.assertEqual(self.client.preset('blue')['entry_transition'], spec)
        for name in ('wipe', 'crossfade'):
            with self.assertRaises(ProtocolError):
                self.client.delete_program(name, program_type='transition')
            changed = compile_source((ROOT / f'examples/transitions/{name}.py').read_text().replace('runtime.progress', 'runtime.progress * runtime.progress')).bytecode
            with self.assertRaises(ProtocolError):
                self.client.save_program(name, changed)
        self.client.save_program('wipe', self.brush)
        self.client.delete_program('wipe', program_type='animation')
        self.client.recall_preset('blue', 'lights')
        self.client.set_power(False, 'lights')
        self.assertEqual(self.frame(), bytes(15))
        self.assertEqual(self.state()['recall'], {'power': False, 'preset': 'blue'})
        # Only the preset owns the transition reference.
        self.save_entry('red', None)
        self.client.delete_program('crossfade', program_type='transition')

    def test_repeated_recalls_reuse_slots_without_heap_growth(self):
        self.save_entry('blue', self.spec('wipe', 0))
        self.client.recall_preset('red', 'lights')
        self.client.recall_preset('blue', 'lights')
        self.client.recall_preset('green', 'lights')
        self.frame()
        baseline = self.client.status_fields()['heap']
        for _ in range(5):
            self.client.recall_preset('blue', 'lights')
            self.client.recall_preset('green', 'lights')
            self.frame()
            self.assertEqual(self.client.status_fields()['heap'], baseline)
