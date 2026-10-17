"""Per-device constructor arguments and startup isolation over the real protocol."""
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

    def devices(self, startups):
        entries = []
        for i, startup in enumerate(startups):
            entry = {'id': f'lights{i}', 'type': 'ws2812b', 'config': {'pin': f'GP{i}', 'count': 5}}
            if startup is not None:
                entry['startup'] = startup
            entries.append(entry)
        return {'schema': 'lightvm.config', 'devices': entries}

    def test_independent_arguments_defaults_and_idle_device_after_reboot(self):
        self.client.save_procedure('rainbow', sample('rainbow'))
        self.client.save_procedure('color', sample('color'))
        candidate = self.devices([
            {'procedure': 'rainbow', 'args': {'duration': 3.3, 'random_color': False}},
            {'procedure': 'rainbow', 'args': {'duration': 7.0, 'scale': 0.0}},
            {'procedure': 'color', 'args': {'color': 0x123456, 'random_color': False}},
            {'procedure': 'rainbow'}, {'procedure': 'rainbow', 'args': {}}, None,
        ])
        self.client.configure(candidate, save=True)
        self.assertEqual(self.client.get_config(), candidate)
        self.assertEqual(self.client.get_config(saved=True), candidate)
        for entry in candidate['devices']:
            self.assertEqual(self.client.status_fields(device=entry['id'])['mode'], 'idle')
        self.restart()
        for i, duration in ((0, 3.3), (1, 7.0), (3, 60.0), (4, 60.0)):
            args = self.client.program_state(device=f'lights{i}')['settings_values']
            self.assertAlmostEqual(args['duration'], duration, places=5)
        self.assertEqual(self.client.program_state(device='lights1')['settings_values']['scale'], 0)
        self.assertEqual(self.client.snapshot(device='lights2')[1], bytes.fromhex('123456') * 5)
        self.assertEqual(self.client.status_fields(device='lights5')['mode'], 'idle')
        self.assertEqual(self.client.describe()['boot_error'], '')
        self.assertEqual(self.client.get_config(), candidate)

    def test_rejects_old_format_and_bad_startup_without_changing_playback(self):
        self.client.save_procedure('rainbow', sample('rainbow'))
        self.client.configure(config())
        self.client.upload(color(17))
        before = self.client.program_state()['generation']
        heap = self.client.status_fields()['heap']
        candidates = []
        for field in ('targets', 'startup'):
            for value in ({}, None, {'procedure': 'rainbow', 'target': 'A'}):
                candidates.append({**config(), field: value})
        for startup in (None, [], {}, {'procedure': ''}, {'procedure': 'rainbow', 'unknown': 1},
                        {'procedure': 'rainbow', 'args': None}, {'procedure': 'rainbow', 'args': []}):
            candidate = config()
            candidate['devices'][0]['startup'] = startup
            candidates.append(candidate)
        for args in ({'unknown': 1}, {'duration': 'fast'}, {'duration': False}, {'duration': 0},
                     {'duration': {}}, {'duration': []}, {'duration': None}, {'random_color': 1}, {'color': -1}):
            candidate = config(startup='rainbow')
            candidate['devices'][0]['startup']['args'] = args
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
            raw = json.dumps(config(startup='rainbow')).replace('"procedure": "rainbow"', '"procedure": "rainbow", "args":' + args)
            with self.assertRaises(ProtocolError):
                self.client.upload_object('config', raw.encode())
        for value in ('fast', 0):
            candidate = config(startup='rainbow')
            candidate['devices'][0]['startup']['args'] = {'duration': value}
            with self.assertRaisesRegex(ProtocolError, r'devices\[lights\]\.startup\.args\.duration'):
                self.client.configure(candidate)

    def test_validation_uses_candidate_geometry_without_applying_it(self):
        self.client.save_procedure('sized', program('''from lightvm import profile,setting,init,render,rgb
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
        candidate['devices'][0]['startup']['args'] = {'size': 6}
        with self.assertRaisesRegex(ProtocolError, 'args.size'):
            self.client.upload_object('config', json.dumps(candidate).encode())
            self.client.manage('config_validate')
        candidate['devices'][0]['config']['count'] = 7
        self.client.upload_object('config', json.dumps(candidate).encode())
        self.client.manage('config_validate')
        self.assertEqual(self.client.get_config(), config(5))
        self.client.require_ok(self.client.command(FRAME, bytes(4)))
        self.assertEqual(self.client.pixel_snapshot, bytes([17, 2, 3]) * 5)
        self.client.configure(candidate, save=True)
        self.restart()
        self.assertEqual(self.client.snapshot()[1], bytes([6, 0, 0]) * 7)

    def test_startup_edits_and_reordering_retain_instances_and_latest_args(self):
        self.client.save_procedure('rainbow', sample('rainbow'))
        candidate = self.devices([{'procedure': 'rainbow', 'args': {'duration': 3.3}}, {'procedure': 'rainbow'}])
        self.client.configure(candidate, save=True)
        self.restart()
        generations = {d['id']: self.client.program_state(device=d['id'])['generation'] for d in candidate['devices']}
        candidate['devices'][0]['startup']['args']['duration'] = 8.0
        candidate['devices'].reverse()
        self.client.configure(candidate, save=True)
        for name, generation in generations.items():
            self.assertEqual(self.client.program_state(device=name)['generation'], generation)
        self.assertAlmostEqual(self.client.program_state(device='lights0')['settings_values']['duration'], 3.3, places=5)
        self.assertEqual(self.client.program_state()['device'], 'lights1')
        self.restart()
        self.assertEqual(self.client.program_state(device='lights0')['settings_values']['duration'], 8.0)
        self.assertEqual(self.client.program_state(device='lights1')['settings_values']['duration'], 60.0)

    def test_one_failed_startup_does_not_block_other_devices(self):
        for name in ('good', 'bad'):
            self.client.save_procedure(name, color(9))
        candidate = self.devices([{'procedure': 'bad'}, {'procedure': 'good'}, None])
        self.client.configure(candidate, save=True)
        self.client.save_procedure('bad', fault())
        self.restart()
        self.assertIn('devices[lights0].startup', self.client.describe()['boot_error'])
        self.assertEqual(self.client.status_fields(device='lights0')['mode'], 'idle')
        self.assertEqual(self.client.snapshot(device='lights1')[1], bytes([9, 2, 3]) * 5)
        self.assertEqual(self.client.get_config(), candidate)
        for name in ('good', 'bad'):
            with self.assertRaisesRegex(ProtocolError, 'referenced'):
                self.client.delete_procedure(name)
        self.client.configure({'schema': 'lightvm.config', 'devices': []})
        for name in ('good', 'bad'):
            with self.assertRaisesRegex(ProtocolError, 'referenced'):
                self.client.delete_procedure(name)
        self.client.save_config()
        for name in ('good', 'bad'):
            self.client.delete_procedure(name)



if __name__ == '__main__':
    unittest.main()
