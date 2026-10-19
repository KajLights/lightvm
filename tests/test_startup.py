"""Preset power-on policy and durable desired state through the real protocol."""
import os
from pathlib import Path
import tempfile
import unittest
import zlib

from bytecode_fixtures import color, program, sample
from test_configuration import ROOT, config
from client import Client, ProcessTransport, ProtocolError


class StartupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.storage = Path(self.temp.name) / 'store.bin'
        self.receiver = os.environ.get('LIGHTVM_RECEIVER', ROOT / 'build/host/lightvm')
        self.client = None
        self.restart()
        self.client.configure(config())
        self.save_preset('red', color(19))
        self.save_preset('blue', color(77))

    def tearDown(self):
        self.client.close()
        self.temp.cleanup()

    def restart(self):
        if self.client:
            self.client.close()
        self.client = Client(ProcessTransport(self.receiver, self.storage))

    def save_preset(self, name, module, settings=None, inputs=None):
        self.client.save_program(name, module)
        layer = {'id': 'main', 'program': name, 'crc32': zlib.crc32(module)}
        if settings is not None:
            layer['settings'] = settings
        if inputs is not None:
            layer['inputs'] = inputs
        self.client.save_preset(name, [layer])

    def policy(self, mode, preset=None):
        candidate = config()
        candidate['outputs'][0]['startup'] = {'mode': mode}
        if preset is not None:
            candidate['outputs'][0]['startup']['preset'] = preset
        return candidate

    def output(self, name='lights'):
        return next(d for d in self.client.describe()['outputs'] if d['id'] == name)

    def test_selected_preset_boots_with_its_layers_settings_and_inputs(self):
        self.save_preset('rainbow', sample('rainbow'), {'random_color': False},
                         {'phase': {'driver': {'kind': 'ramp', 'duration': 3.3, 'mode': 'repeat'}}})
        candidate = self.policy('preset', 'rainbow')
        self.client.configure(candidate, save=True)
        self.assertFalse(self.output()['power'])
        self.restart()
        self.assertTrue(self.output()['power'])
        self.assertEqual(self.output()['selected_preset'], 'rainbow')
        self.assertEqual(self.client.program_state(layer='main', slot='B')['inputs']['phase']['binding']['driver']['duration'], 3.3)
        self.assertEqual(self.client.get_config(), candidate)
        self.assertEqual(self.client.describe()['boot_error'], '')

    def test_default_and_explicit_off_ignore_previous_on_state(self):
        for candidate in (config(), self.policy('off')):
            self.client.configure(candidate, save=True)
            self.client.recall_preset('red')
            self.restart()
            self.assertFalse(self.output()['power'])
            self.assertEqual(self.client.status_fields()['mode'], 'idle')

    def test_restore_last_survives_abrupt_restart_on_and_off(self):
        self.client.configure(self.policy('restore_last'), save=True)
        self.restart()
        self.assertFalse(self.output()['power'])
        self.client.recall_preset('red')
        self.restart()
        self.assertTrue(self.output()['power'])
        self.assertEqual(self.output()['selected_preset'], 'red')
        self.client.set_power(False)
        self.restart()
        self.assertFalse(self.output()['power'])
        self.assertEqual(self.output()['selected_preset'], 'red')
        self.assertEqual(self.client.status_fields()['mode'], 'idle')
        self.client.set_power(True)
        self.assertEqual(self.output()['selected_preset'], 'red')

    def test_explicit_off_after_a_stopped_preview_updates_remembered_power(self):
        self.client.configure(self.policy('restore_last'), save=True)
        self.client.recall_preset('red')
        self.client.upload(color(31))
        self.client.set_power(False)
        self.restart()
        self.assertFalse(self.output()['power'])
        self.assertEqual(self.output()['selected_preset'], 'red')

    def test_explicit_off_on_an_off_boot_replaces_previous_on_target(self):
        self.client.configure(self.policy('off'), save=True)
        self.client.recall_preset('red')
        self.restart()
        self.client.set_power(False)
        self.client.configure(self.policy('restore_last'), save=True)
        self.restart()
        self.assertFalse(self.output()['power'])
        self.assertEqual(self.output()['selected_preset'], 'red')

    def test_restore_tracks_preset_selection_but_ignores_editor_preview(self):
        self.client.configure(self.policy('restore_last'), save=True)
        self.client.recall_preset('red')
        self.client.recall_preset('blue')
        self.client.upload(color(31))
        self.restart()
        self.assertEqual(self.output()['selected_preset'], 'blue')
        self.assertTrue(self.output()['power'])

    def test_repeated_commands_and_boot_do_not_rewrite_flash(self):
        self.client.configure(self.policy('restore_last'), save=True)
        self.client.recall_preset('red')
        snapshot = self.storage.read_bytes()
        self.client.recall_preset('red')
        self.client.set_power(True)
        self.assertEqual(self.storage.read_bytes(), snapshot)
        self.restart()
        self.assertEqual(self.storage.read_bytes(), snapshot)
        self.assertEqual([p['name'] for p in self.client.presets()], ['red', 'blue'])
        self.assertEqual({p['name'] for p in self.client.programs()}, {'red', 'blue'})

    def test_restore_target_is_saved_before_a_long_fade_finishes(self):
        module = program((ROOT / 'examples/transitions/wipe.py').read_text())
        self.client.save_program('wipe', module)
        transition = {'program': 'wipe', 'crc32': zlib.crc32(module), 'duration_ms': 60000}
        candidate = self.policy('restore_last')
        self.client.save_preset('red', self.client.preset('red')['layers'], entry_transition=transition)
        self.client.configure(candidate, save=True)
        self.client.recall_preset('red')
        self.assertLess(self.client.transition()['progress'], 100)
        self.client.set_power(False)
        self.restart()
        self.assertFalse(self.output()['power'])
        self.assertEqual(self.output()['selected_preset'], 'red')
        self.assertEqual(self.client.status_fields()['mode'], 'idle')
        self.client.set_power(True)
        self.restart()
        self.assertTrue(self.output()['power'])
        self.assertEqual(self.output()['selected_preset'], 'red')
        self.assertEqual(self.client.transition()['program'], 'wipe')

    def test_each_output_restores_by_identity_after_reordering(self):
        candidate = self.policy('restore_last')
        candidate['outputs'].append({'id': 'desk', 'type': 'ws2812b', 'config': {'pin': 'GP17', 'count': 5}, 'startup': {'mode': 'preset', 'preset': 'blue'}})
        self.client.configure(candidate, save=True)
        self.client.recall_preset('red', output='lights')
        candidate['outputs'].reverse()
        self.client.configure(candidate, save=True)
        self.assertEqual(self.output()['selected_preset'], 'red')
        self.restart()
        self.assertEqual(self.output()['selected_preset'], 'red')
        self.assertEqual(self.output('desk')['selected_preset'], 'blue')

    def test_startup_and_power_use_the_same_preset_transition(self):
        module = program((ROOT / 'examples/transitions/wipe.py').read_text())
        self.client.save_program('wipe', module)
        spec = {'program': 'wipe', 'crc32': zlib.crc32(module), 'duration_ms': 5000}
        self.client.save_preset('red', self.client.preset('red')['layers'], entry_transition=spec)
        self.client.configure(self.policy('preset', 'red'), save=True)
        self.restart()
        for power in (None, False, True):
            if power is not None:
                self.client.set_power(power)
            state = self.client.transition()
            self.assertEqual(state['program'], 'wipe')
            self.assertEqual(state['driver']['duration_ms'], 5000)
            self.assertLess(state['progress'], 100)

    def test_startup_policy_edits_take_effect_only_on_reboot(self):
        self.client.configure(self.policy('preset', 'red'), save=True)
        self.restart()
        self.client.configure(self.policy('preset', 'blue'), save=True)
        self.assertEqual(self.output()['selected_preset'], 'red')
        self.restart()
        self.assertEqual(self.output()['selected_preset'], 'blue')

    def test_switching_from_fixed_preset_to_restore_last_captures_current_target(self):
        self.client.configure(self.policy('preset', 'red'), save=True)
        self.restart()
        self.client.configure(self.policy('restore_last'), save=True)
        self.restart()
        self.assertEqual(self.output()['selected_preset'], 'red')
        self.assertTrue(self.output()['power'])

    def test_invalid_startup_retains_playback_and_configuration(self):
        self.client.configure(config())
        self.client.recall_preset('red')
        before = self.client.transition()['revision']
        for startup in (None, [], {}, {'program': 'red'}, {'mode': 'last'}, {'mode': 'preset'},
                        {'mode': 'preset', 'preset': ''}, {'mode': 'preset', 'preset': 'missing'},
                        {'mode': 'off', 'preset': 'red'}, {'mode': 'restore_last', 'settings': {}},
                        {'mode': 'preset', 'preset': 'red', 'settings': {}}):
            candidate = config()
            candidate['outputs'][0]['startup'] = startup
            with self.subTest(startup=startup), self.assertRaises(ProtocolError):
                self.client.configure(candidate)
            self.assertEqual(self.client.get_config(), config())
            self.assertEqual(self.client.transition()['revision'], before)
        for field in ('startup', 'targets'):
            with self.assertRaises(ProtocolError):
                self.client.configure({**config(), field: {}})

    def test_candidate_geometry_is_used_without_changing_live_output(self):
        self.save_preset('sized', program('''from lightvm import profile,setting,init,render,rgb
profile("animation")
@init
def setup(ctx,runtime):
 ctx.size=setting(5,minimum=1,maximum=runtime.pixel_count)
@render
def draw(ctx,runtime,pixels): pixels.fill(rgb(ctx.size,0,0))
'''))
        candidate = self.policy('preset', 'sized')
        candidate['outputs'][0]['config']['count'] = 3
        with self.assertRaises(ProtocolError):
            self.client.configure(candidate)
        self.assertEqual(self.client.get_config(), config())

    def test_configured_presets_are_protected_until_active_and_saved_refs_are_removed(self):
        self.client.configure(self.policy('preset', 'red'), save=True)
        with self.assertRaisesRegex(ProtocolError, 'referenced'):
            self.client.delete_preset('red')
        self.client.configure(config())
        with self.assertRaisesRegex(ProtocolError, 'referenced'):
            self.client.delete_preset('red')
        self.client.save_config()
        self.client.delete_preset('red')

    def test_missing_restore_preset_leaves_output_off_and_management_available(self):
        self.client.configure(self.policy('restore_last'), save=True)
        self.client.recall_preset('red')
        self.client.delete_preset('red')
        self.restart()
        self.assertFalse(self.output()['power'])
        self.assertIn('outputs[lights].startup', self.client.describe()['boot_error'])
        self.client.recall_preset('blue')
        self.restart()
        self.assertEqual(self.output()['selected_preset'], 'blue')

    def test_full_store_rejects_recall_without_changing_live_animation(self):
        self.client.configure(self.policy('restore_last'), save=True)
        self.client.activate('red')
        before = self.client.program_state()['generation']
        module = color(5)
        for n in range(200):
            try:
                self.client.save_program(f'fill{n}', module)
            except ProtocolError:
                break
        else:
            self.fail('test did not fill storage')
        with self.assertRaisesRegex(ProtocolError, 'persist'):
            self.client.recall_preset('blue')
        self.assertEqual(self.client.program_state()['generation'], before)
        self.assertEqual(self.client.describe()['active_program'], 'red')
        self.assertEqual(self.client.status_fields()['mode'], 'bytecode')
        self.assertIsNone(self.client.transition()['recall'])
        self.restart()
        self.assertFalse(self.output()['power'])

    def test_one_invalid_startup_does_not_block_other_outputs(self):
        candidate = self.policy('preset', 'red')
        candidate['outputs'].append({'id': 'desk', 'type': 'ws2812b', 'config': {'pin': 'GP17', 'count': 8}, 'startup': {'mode': 'preset', 'preset': 'blue'}})
        self.client.configure(candidate, save=True)
        # This replacement is valid on desk but cannot initialize on lights.
        module = program('''from lightvm import profile,setting,init,render,rgb
profile("animation")
@init
def setup(ctx,runtime):
 ctx.size=setting(7,minimum=1,maximum=runtime.pixel_count)
@render
def draw(ctx,runtime,pixels): pixels.fill(rgb(ctx.size,0,0))
''')
        self.client.save_program('sized', module)
        self.client.save_preset('red', [{'id': 'main', 'program': 'sized', 'crc32': zlib.crc32(module)}], output='desk')
        self.restart()
        self.assertFalse(self.output()['power'])
        self.assertEqual(self.output('desk')['selected_preset'], 'blue')
        self.assertIn('outputs[lights].startup', self.client.describe()['boot_error'])


if __name__ == '__main__':
    unittest.main()
