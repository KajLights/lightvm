"""Typed storage through the real compiler, controller and Python client."""
import os
from pathlib import Path
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from client import Client, ProcessTransport, ProtocolError
from lightvm.compiler import compile_source


class StorageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / 'flash.bin'
        self.client = None
        self.restart()
        self.animation = compile_source('from lightvm import profile,render,rgb\nprofile("animation")\n'
                                        '@render\ndef draw(ctx,runtime,pixels):\n pixels.fill(rgb(12,34,56))\n').bytecode
        self.transition = compile_source((ROOT / 'examples/transitions/wipe.py').read_text()).bytecode
        self.layers = [{'id': 'base', 'program': 'wipe', 'crc32': zlib.crc32(self.animation)}]
        self.config = {'schema': 'lightvm.config', 'outputs': [
            {'id': 'lights', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': 5}}]}
        self.client.configure(self.config)

    def restart(self):
        if self.client:
            self.client.close()
        receiver = os.environ.get('LIGHTVM_RECEIVER', str(ROOT / 'build/storage-host/lightvm'))
        self.client = Client(ProcessTransport(receiver, self.path))

    def tearDown(self):
        self.client.close()
        self.directory.cleanup()

    def save_both(self):
        self.client.save_program('wipe', self.animation)
        self.client.save_program('wipe', self.transition)

    def test_same_name_dispatch_and_filtered_listing_survive_reboot(self):
        self.save_both()
        self.client.save_preset('wipe', self.layers, output='lights')
        self.config['outputs'][0]['startup'] = {'mode': 'preset', 'preset': 'wipe'}
        self.client.configure(self.config, save=True)
        self.restart()
        self.assertEqual(self.client.status_fields()['mode'], 'transition')
        self.assertEqual(self.client.get_program('wipe', 'animation'), self.animation)
        self.assertEqual(self.client.get_program('wipe', 'transition'), self.transition)
        for kind in ('animation', 'transition'):
            listing = self.client.programs(kind)
            self.assertEqual([(p['name'], p['type']) for p in listing], [('wipe', kind)])
        with self.assertRaisesRegex(ProtocolError, 'specify animation or transition type'):
            self.client.get_program('wipe')
        with self.assertRaisesRegex(ProtocolError, 'specify animation or transition type'):
            self.client.delete_program('wipe')
        self.client.activate_preset('wipe', output='lights')
        self.client.select_transition('wipe', output='lights')
        self.assertEqual(self.client.transition('lights')['program'], 'wipe')

    def test_type_specific_reference_checks_and_replacement(self):
        self.save_both()
        self.client.save_preset('wipe', self.layers, output='lights')
        replacement = compile_source((ROOT / 'examples/transitions/crossfade.py').read_text()).bytecode
        self.client.save_program('wipe', replacement)
        self.assertEqual(len(self.client.programs()), 2)
        self.assertEqual(self.client.get_program('wipe', 'transition'), replacement)
        self.client.delete_program('wipe', 'transition')
        self.assertEqual(self.client.get_program('wipe'), self.animation)
        with self.assertRaisesRegex(ProtocolError, 'referenced'):
            self.client.delete_program('wipe', 'animation')
        self.assertEqual(self.client.preset('wipe')['layers'], self.layers)
        self.client.save_preset('only_preset', self.layers, output='lights')
        for operation in ('program_get', 'program_delete'):
            with self.assertRaisesRegex(ProtocolError, 'does not exist'):
                self.client.manage(operation, name='only_preset', expected_revision=self.client.describe()['catalog_revision'])
        self.assertEqual(self.client.preset('only_preset')['layers'], self.layers)

    def test_explicit_save_type_must_match_the_uploaded_profile(self):
        self.client.upload_object('program', self.transition, 'wipe')
        revision = self.client.describe()['catalog_revision']
        with self.assertRaisesRegex(ProtocolError, 'type differs'):
            self.client.manage('program_save', name='wipe', type='animation', expected_revision=revision)
        self.assertEqual(self.client.programs(), [])
        self.client.manage('program_save', name='wipe', type='transition', expected_revision=revision)
        self.assertEqual(self.client.get_program('wipe', 'transition'), self.transition)
        with self.assertRaises(ProtocolError):
            self.client.manage('program_list', type='unknown')

    def test_capacity_is_reported_without_count_quotas(self):
        description = self.client.describe()
        self.assertTrue(description['typed_storage'])
        self.assertTrue(description['storage_writable'])
        self.assertEqual(description['storage_backend'], 'extent')
        self.assertNotIn('program_limit', description)
        self.assertNotIn('preset_limit', description)
        before = description['free_bytes']
        for n in range(20):
            self.client.save_program('program' + str(n), self.animation)
        self.assertEqual(len(self.client.programs('animation')), 20)
        self.assertEqual(self.client.programs('transition'), [])
        self.assertLess(self.client.describe()['free_bytes'], before)
        self.restart()
        self.assertEqual(len(self.client.programs('animation')), 20)
