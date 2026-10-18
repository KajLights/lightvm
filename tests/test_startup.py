"""Per-output constructor arguments and startup isolation over the real protocol."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from bytecode_fixtures import color, fault, program, sample
from test_configuration import ROOT, config
from client import Client, ProcessTransport, ProtocolError, FRAME


class StartupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.storage = Path(self.temp.name) / 'store.bin'
        self.receiver = os.environ.get('LIGHTVM_RECEIVER', ROOT / 'build/host/lightvm')
        self.client = None
        self.restart()

    def tearDown(self):
        self.client.close()
        self.temp.cleanup()

    def restart(self):
        if self.client:
            self.client.close()
        self.client = Client(ProcessTransport(self.receiver, self.storage))

    def outputs(self, startups):
        entries = []
        for i, startup in enumerate(startups):
            entry = {'id': f'lights{i}', 'type': 'ws2812b', 'config': {'pin': f'GP{i}', 'count': 5}}
            if startup is not None:
                entry['startup'] = startup
            entries.append(entry)
        return {'schema': 'lightvm.config', 'outputs': entries}

    def test_independent_arguments_defaults_and_idle_device_after_reboot(self):
        self.client.save_program('rainbow', sample('rainbow'))
        self.client.save_program('color', sample('color'))
        candidate = self.outputs([
            {'program': 'rainbow', 'settings': {'random_color': False}, 'inputs': {'phase': {'driver': {'kind': 'ramp', 'duration': 3.3, 'mode': 'repeat'}}}},
            {'program': 'rainbow', 'inputs': {'phase': {'driver': {'kind': 'ramp', 'duration': 7.0, 'mode': 'repeat'}}, 'scale': {'value': 0.0}}},
            {'program': 'color', 'settings': {'color': 0x123456, 'random_color': False}},
            {'program': 'rainbow'}, {'program': 'rainbow', 'settings': {}}, None,
        ])
        self.client.configure(candidate, save=True)
        self.assertEqual(self.client.get_config(), candidate)
        self.assertEqual(self.client.get_config(saved=True), candidate)
        for entry in candidate['outputs']:
            self.assertEqual(self.client.status_fields(output=entry['id'])['mode'], 'idle')
        self.restart()
        for i, duration in ((0, 3.3), (1, 7.0), (3, 60.0), (4, 60.0)):
            inputs = self.client.program_state(output=f'lights{i}')['inputs']
            self.assertAlmostEqual(inputs['phase']['binding']['driver']['duration'], duration, places=5)
        self.assertEqual(self.client.program_state(output='lights1')['inputs']['scale']['value'], 0)
        self.assertEqual(self.client.snapshot(output='lights2')[1], bytes.fromhex('123456') * 5)
        self.assertEqual(self.client.status_fields(output='lights5')['mode'], 'idle')
        self.assertEqual(self.client.describe()['boot_error'], '')
        self.assertEqual(self.client.get_config(), candidate)

    def test_rejects_old_format_and_bad_startup_without_changing_playback(self):
        self.client.save_program('rainbow', sample('rainbow'))
        self.client.configure(config())
        self.client.upload(color(17))
        before = self.client.program_state()['generation']
        heap = self.client.status_fields()['heap']
        candidates = []
        for field in ('targets', 'startup'):
            for value in ({}, None, {'program': 'rainbow', 'target': 'A'}):
                candidates.append({**config(), field: value})
        for startup in (None, [], {}, {'program': ''}, {'program': 'rainbow', 'unknown': 1},
                        {'program': 'rainbow', 'settings': None}, {'program': 'rainbow', 'settings': []}):
            candidate = config()
            candidate['outputs'][0]['startup'] = startup
            candidates.append(candidate)
        for args in ({'unknown': 1}, {'duration': 'fast'}, {'duration': False}, {'duration': 0},
                     {'duration': {}}, {'duration': []}, {'duration': None}, {'random_color': 1}, {'color': -1}):
            candidate = config(startup='rainbow')
            candidate['outputs'][0]['startup']['settings'] = args
            candidates.append(candidate)
        for candidate in candidates:
            with self.subTest(candidate=candidate), self.assertRaises(ProtocolError):
                self.client.configure(candidate)
            self.assertEqual(self.client.get_config(), config())
            self.assertEqual(self.client.program_state()['generation'], before)
            self.assertEqual(self.client.status_fields()['heap'], heap)
            self.client.require_ok(self.client.command(FRAME, bytes(4)))
            self.assertEqual(self.client.pixel_snapshot[0], 17)
        for args in ('{"duration":3,"duration":4}', '{"duration":3,"durat\\u0069on":4}'):
            raw = json.dumps(config(startup='rainbow')).replace('"program": "rainbow"', '"program": "rainbow", "settings":' + args)
            with self.assertRaises(ProtocolError):
                self.client.upload_object('config', raw.encode())
        for value in ('fast', 0):
            candidate = config(startup='rainbow')
            candidate['outputs'][0]['startup']['settings'] = {'duration': value}
            with self.assertRaisesRegex(ProtocolError, r'outputs\[lights\]\.startup\.settings\.duration'):
                self.client.configure(candidate)

    def test_validation_uses_candidate_geometry_without_applying_it(self):
        self.client.save_program('sized', program('''from lightvm import profile,setting,init,render,rgb
profile("animation")
@init
def setup(ctx,runtime):
 ctx.size=setting(1,minimum=1,maximum=runtime.pixel_count)
@render
def draw(ctx,runtime,pixels): pixels.fill(rgb(ctx.size,0,0))
'''))
        self.client.configure(config(5))
        self.client.upload(color(17))
        candidate = config(5, startup='sized')
        candidate['outputs'][0]['startup']['settings'] = {'size': 6}
        with self.assertRaisesRegex(ProtocolError, 'settings.size'):
            self.client.upload_object('config', json.dumps(candidate).encode())
            self.client.manage('config_validate')
        candidate['outputs'][0]['config']['count'] = 7
        self.client.upload_object('config', json.dumps(candidate).encode())
        self.client.manage('config_validate')
        self.assertEqual(self.client.get_config(), config(5))
        self.client.require_ok(self.client.command(FRAME, bytes(4)))
        self.assertEqual(self.client.pixel_snapshot, bytes([17, 2, 3]) * 5)
        self.client.configure(candidate, save=True)
        self.restart()
        self.assertEqual(self.client.snapshot()[1], bytes([6, 0, 0]) * 7)

    def test_startup_edits_and_reordering_retain_instances_and_latest_args(self):
        self.client.save_program('rainbow', sample('rainbow'))
        candidate = self.outputs([{'program': 'rainbow', 'inputs': {'phase': {'driver': {'kind': 'ramp', 'duration': 3.3, 'mode': 'repeat'}}}}, {'program': 'rainbow'}])
        self.client.configure(candidate, save=True)
        self.restart()
        generations = {d['id']: self.client.program_state(output=d['id'])['generation'] for d in candidate['outputs']}
        candidate['outputs'][0]['startup']['inputs']['phase']['driver']['duration'] = 8.0
        candidate['outputs'].reverse()
        self.client.configure(candidate, save=True)
        for name, generation in generations.items():
            self.assertEqual(self.client.program_state(output=name)['generation'], generation)
        self.assertAlmostEqual(self.client.program_state(output='lights0')['inputs']['phase']['binding']['driver']['duration'], 3.3, places=5)
        self.assertEqual(self.client.program_state()['output'], 'lights1')
        self.restart()
        self.assertEqual(self.client.program_state(output='lights0')['inputs']['phase']['binding']['driver']['duration'], 8.0)
        self.assertEqual(self.client.program_state(output='lights1')['inputs']['phase']['binding']['driver']['duration'], 60.0)

    def test_one_failed_startup_does_not_block_other_devices(self):
        for name in ('good', 'bad'):
            self.client.save_program(name, color(9))
        candidate = self.outputs([{'program': 'bad'}, {'program': 'good'}, None])
        self.client.configure(candidate, save=True)
        self.client.save_program('bad', fault())
        self.restart()
        self.assertIn('outputs[lights0].startup', self.client.describe()['boot_error'])
        self.assertEqual(self.client.status_fields(output='lights0')['mode'], 'idle')
        self.assertEqual(self.client.snapshot(output='lights1')[1], bytes([9, 2, 3]) * 5)
        self.assertEqual(self.client.get_config(), candidate)
        for name in ('good', 'bad'):
            with self.assertRaisesRegex(ProtocolError, 'referenced'):
                self.client.delete_program(name)
        self.client.configure({'schema': 'lightvm.config', 'outputs': []})
        for name in ('good', 'bad'):
            with self.assertRaisesRegex(ProtocolError, 'referenced'):
                self.client.delete_program(name)
        self.client.save_config()
        for name in ('good', 'bad'):
            self.client.delete_program(name)



if __name__ == '__main__':
    unittest.main()
