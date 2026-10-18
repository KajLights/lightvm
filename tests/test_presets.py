"""Persistent recipes exercise the real store, VM and protocol across restarts."""
import copy
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from client import Client, ProcessTransport, ProtocolError, FRAME, STOP, OBJECT_CANCEL
from lightvm.compiler import compile_source

SOURCE = '''from lightvm import profile, setting, init, render, rgb
profile("animation")
@init
def setup(ctx, runtime):
 ctx.color = setting(rgb(255, 0, 0))
@render
def draw(ctx, runtime, pixels):
 pixels.fill(ctx.color)
'''


class PresetTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.storage = Path(self.directory.name) / 'store.bin'
        self.client = None
        self.restart()
        self.config = {'schema': 'lightvm.config', 'outputs': [
            {'id': 'first', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': 5}},
            {'id': 'second', 'type': 'ws2812b', 'config': {'pin': 'GP17', 'count': 5}}]}
        self.client.configure(self.config, save=True)
        self.program = compile_source(SOURCE).bytecode
        self.client.save_program('brush', self.program)
        self.layers = [{'id': 'eye', 'program': 'brush', 'crc32': zlib.crc32(self.program),
                        'settings': {'color': 0x0000ff}, 'blend': 'screen', 'opacity': 255}]

    def tearDown(self):
        self.client.close()
        self.directory.cleanup()

    def restart(self):
        if self.client:
            self.client.close()
        self.client = Client(ProcessTransport(os.environ.get('LIGHTVM_RECEIVER', ROOT / 'build/unified-animation-host/lightvm'), self.storage))

    def frame(self, output='second'):
        self.client.require_ok(self.client.command(STOP, output=output))
        self.client.require_ok(self.client.command(FRAME, struct.pack('<I', 0), output=output))
        return bytes(self.client.pixel_snapshot)

    def test_preset_survives_restart_and_runs_on_selected_output(self):
        self.client.save_preset('blue', self.layers, output='second')
        self.assertEqual(self.client.preset('blue'), {'schema': 'lightvm.preset', 'layers': self.layers})
        self.assertEqual([p['name'] for p in self.client.programs()], ['brush'])
        self.restart()
        self.assertEqual([p['name'] for p in self.client.presets()], ['blue'])
        self.assertFalse(self.client.composition('first')['active'])
        self.assertFalse(self.client.composition('second')['active'])
        self.client.activate_preset('blue', output='second')
        self.assertEqual(self.frame(), bytes([0, 0, 255]) * 5)
        self.assertFalse(self.client.composition('first')['active'])
        self.assertEqual(self.client.get_config(saved=True), self.config)

    def test_versions_are_protected_until_last_preset_is_deleted(self):
        for name in ('one', 'two'):
            self.client.save_preset(name, self.layers, output='second')
        with self.assertRaises(ProtocolError):
            self.client.delete_program('brush')
        changed = compile_source(SOURCE.replace('pixels.fill(ctx.color)', 'pixels.fill(rgb(0, 255, 0))')).bytecode
        with self.assertRaises(ProtocolError):
            self.client.save_program('brush', changed)
        self.client.require_ok(self.client.command(OBJECT_CANCEL))
        self.client.delete_preset('one')
        with self.assertRaises(ProtocolError):
            self.client.delete_program('brush')
        self.client.delete_preset('two')
        self.client.save_program('brush', changed)
        self.client.delete_program('brush')
        self.assertEqual(self.client.programs(), [])

    def test_invalid_recipe_retains_playback_catalog_and_memory(self):
        self.client.save_preset('blue', self.layers, output='second')
        self.client.activate_preset('blue', output='second')
        expected = self.frame()
        revision = self.client.describe()['catalog_revision']
        heap = self.client.status_fields()['heap']
        invalid = []
        for patch in ({'crc32': 1}, {'program': 'missing'}, {'blend': 'invalid'}, {'settings': {'unknown': 3}}):
            invalid.append([{**self.layers[0], **patch}])
        without_crc = copy.deepcopy(self.layers)
        del without_crc[0]['crc32']
        invalid += [without_crc, [], self.layers * 2]
        for layers in invalid:
            with self.subTest(layers=layers), self.assertRaises(ProtocolError):
                self.client.save_preset('blue', layers, output='second')
            self.assertEqual(self.client.describe()['catalog_revision'], revision)
            self.assertEqual(self.frame(), expected)
            self.assertEqual(self.client.status_fields()['heap'], heap)
        self.assertEqual(self.client.preset('blue')['layers'], self.layers)

    def test_programs_and_presets_share_capacity_without_count_quotas(self):
        for n in range(12):
            self.client.save_program('program' + str(n), self.program)
        for n in range(9):
            self.client.save_preset('preset' + str(n), self.layers, output='second')
        self.client.configure_network({'schema': 'lightvm.network', 'wifi': {'enabled': False, 'country': 'XX', 'networks': []}, 'http': {'enabled': False, 'port': 80}})
        self.client.save_preset('another', self.layers, output='second')
        self.restart()
        self.assertEqual(len(self.client.presets()), 10)
        self.assertEqual(len(self.client.programs()), 13)
        self.client.activate_preset('preset4', output='second')
        self.assertEqual(self.frame(), bytes([0, 0, 255]) * 5)

    def test_replacing_and_deleting_recipe_preserves_running_instances(self):
        self.client.save_preset('blue', self.layers, output='second')
        self.client.activate_preset('blue', output='second')
        original = self.client.program_state(output='second', layer='eye')['generation']
        red = [{**self.layers[0], 'settings': {'color': 0xff0000}}]
        self.client.save_preset('blue', red, output='second')
        self.assertEqual(self.frame(), bytes([0, 0, 255]) * 5)
        self.client.delete_preset('blue')
        self.assertEqual(self.client.program_state(output='second', layer='eye')['generation'], original)
        self.assertEqual(self.frame(), bytes([0, 0, 255]) * 5)
        with self.assertRaises(ProtocolError):
            self.client.delete_program('brush')

    def test_stale_catalog_and_composition_revisions_are_rejected(self):
        self.client.save_preset('blue', self.layers, output='second')
        revision = self.client.describe()['catalog_revision']
        self.client.save_preset('red', self.layers, output='second')
        with self.assertRaises(ProtocolError):
            self.client.manage('preset_delete', name='blue', expected_revision=revision)
        composition_revision = self.client.composition('second')['revision']
        self.client.activate_preset('blue', output='second')
        with self.assertRaises(ProtocolError):
            self.client.manage('preset_activate', name='blue', output='second', expected_revision=composition_revision)

    def test_preview_initializes_settings_before_replacing_the_selected_output(self):
        self.client.save_preset('blue', self.layers, output='second')
        self.client.activate_preset('blue', output='second')
        expected = self.frame()
        with self.assertRaises(ProtocolError):
            self.client.upload(self.program, output='second', settings={'unknown': 1})
        self.assertEqual(self.frame(), expected)
        self.client.upload(self.program, output='second', settings={'color': 0x00ff00})
        self.assertEqual(self.client.program_state(output='second')['settings_values'], {'color': 0x00ff00})
        self.assertEqual(self.frame(), bytes([0, 255, 0]) * 5)
        self.assertFalse(self.client.composition('first')['active'])
