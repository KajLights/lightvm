"""Power-recovery policy and full HTTP API against the host's real engine.

The host models boot policy with a loopback listener override, not a physical AP.
The fake radio policy test and native builds cover the interface configuration.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import unittest
import zlib
from bytecode_fixtures import color, sample, padded_program

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from client import Client, HttpTransport, ProcessTransport


class PowerRecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.receiver = Path(os.environ.get('LIGHTVM_RECOVERY_RECEIVER', ROOT / 'build/power-recovery-host/lightvm'))
        if not cls.receiver.exists():
            raise unittest.SkipTest('Build a host receiver with WiFi, HTTP and boot sequence enabled')
        probe = ProcessTransport(cls.receiver)
        try:
            if not Client(probe).describe().get('recovery_supported'):
                raise unittest.SkipTest('Power recovery excluded from this build')
        finally:
            probe.close()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.storage = Path(self.directory.name) / 'store.bin'
        self.process = None
        self.http_transport = None
        self.start()

    def start(self):
        self.process = subprocess.Popen([str(self.receiver), '--storage', str(self.storage), '--http', '0'],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        line = self.process.stderr.readline().decode()
        self.assertIn('HTTP listening', line)
        self.url = re.search(r'http://\S+', line)[0]
        pipe = ProcessTransport.__new__(ProcessTransport)
        pipe.process = self.process
        self.pipe = Client(pipe)
        self.http_transport = HttpTransport(self.url)
        self.http = Client(self.http_transport)

    def stop(self):
        if self.http_transport:
            self.http_transport.close()
            self.http_transport = None
        if self.process:
            self.process.terminate()
            self.process.wait(timeout=5)
            for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
                stream.close()
            self.process = None

    def restart(self):
        self.stop()
        self.start()

    def tearDown(self):
        self.stop()
        self.directory.cleanup()

    def saved_profile(self):
        return {'schema': 'lightvm.network', 'wifi': {
            'enabled': False, 'mode': 'station', 'country': 'ZZ',
            'networks': [{'ssid': 'studio', 'password': 'station-secret'}],
            'ap': {'ssid': 'private-lights', 'password': 'saved-ap-secret'},
        }, 'http': {'enabled': False, 'port': 8080}}

    def save_network(self, config):
        self.http.configure_network(config)
        deadline = time.monotonic() + 2
        while self.http.network_status()['state'] == 'reconfiguring':
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)

    def enter_recovery(self):
        self.restart()
        self.assertFalse(self.pipe.network_status()['recovery'])
        self.restart()
        self.assertTrue(self.pipe.network_status()['recovery'])

    def test_recovery_preserves_saved_settings_and_keeps_http_after_saves(self):
        self.save_network(self.saved_profile())
        saved = self.http.network_config()
        snapshot = self.storage.read_bytes()
        self.enter_recovery()
        status = self.http.network_status()
        self.assertEqual((status['mode'], status['http_port'], status['initial_setup']), ('ap', 80, False))
        self.assertEqual(self.http.network_config(), saved)
        self.assertEqual(self.storage.read_bytes(), snapshot)
        self.assertEqual(self.http.describe()['network_revision'], 1)
        # Renaming the saved AP retains its password, while the runtime AP stays open.
        saved['wifi']['ap']['ssid'] = 'renamed-lights'
        saved['http']['port'] = 9000
        self.save_network(saved)
        self.assertEqual(self.http.network_config(), saved)
        self.assertIn(b'saved-ap-secret', self.storage.read_bytes())
        self.assertIn(b'station-secret', self.storage.read_bytes())
        for client in (self.http, self.pipe):
            status = client.network_status()
            self.assertTrue(status['recovery'])
            self.assertEqual(status['http_port'], 80)
            self.assertNotEqual(status['state'], 'reconfiguring')
            self.assertEqual(client.describe()['network_revision'], 2)
        # The same HTTP session still provides library management and discovery.
        self.http.save_program('empty', color())
        self.assertEqual(self.pipe.get_program('empty'), color())
        with self.http_transport.opener.open(self.url + '/api/v1/controllers') as response:
            self.assertEqual(json.load(response)['schema'], 'lightvm.discovery')

    def test_saved_startup_is_idle_in_recovery_and_can_be_activated_manually(self):
        module = color()
        self.http.save_program('startup', module)
        config = {'schema': 'lightvm.config', 'outputs': [
            {'id': 'lights', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': 5}, 'startup': {'mode': 'preset', 'preset': 'startup'}},
        ]}
        hardware = {**config, 'outputs': [{k: v for k, v in config['outputs'][0].items() if k != 'startup'}]}
        self.http.configure(hardware)
        self.http.save_preset('startup', [{'id': 'main', 'program': 'startup', 'crc32': zlib.crc32(module)}])
        self.http.configure(config, save=True)
        snapshot = self.storage.read_bytes()
        self.restart()
        self.assertEqual(self.http.describe()['outputs'][0]['selected_preset'], 'startup')
        self.restart()
        self.assertTrue(self.http.network_status()['recovery'])
        self.assertIsNone(self.http.describe()['outputs'][0]['selected_preset'])
        self.assertEqual(self.pipe.status_fields()['mode'], 'idle')
        self.assertEqual(self.http.get_config(saved=True), config)
        self.assertEqual(self.storage.read_bytes(), snapshot)
        self.http.activate('startup')
        self.assertEqual(self.pipe.describe()['active_program'], 'startup')
        self.assertTrue(self.http.network_status()['recovery'])

    def test_restore_last_recovery_keeps_the_remembered_target(self):
        module = color()
        hardware = {'schema': 'lightvm.config', 'outputs': [
            {'id': 'lights', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': 5}}]}
        self.http.configure(hardware)
        self.http.save_program('startup', module)
        self.http.save_preset('startup', [{'id': 'main', 'program': 'startup', 'crc32': zlib.crc32(module)}])
        hardware['outputs'][0]['startup'] = {'mode': 'restore_last'}
        self.http.configure(hardware, save=True)
        self.http.recall_preset('startup')
        snapshot = self.storage.read_bytes()
        self.restart()
        self.assertTrue(self.http.describe()['outputs'][0]['power'])
        self.restart()
        self.assertTrue(self.http.network_status()['recovery'])
        self.assertFalse(self.http.describe()['outputs'][0]['power'])
        self.assertEqual(self.storage.read_bytes(), snapshot)
        deadline = time.monotonic() + 6
        while self.http.describe()['boot_sequence']['count']:
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.05)
        self.restart()
        self.assertFalse(self.http.network_status()['recovery'])
        self.assertEqual(self.http.describe()['outputs'][0]['selected_preset'], 'startup')
        self.assertTrue(self.http.describe()['outputs'][0]['power'])
        self.assertEqual(self.storage.read_bytes(), snapshot)

    def test_counter_clear_retains_recovery_until_next_restart(self):
        self.save_network(self.saved_profile())
        self.enter_recovery()
        deadline = time.monotonic() + 6
        while self.http.describe()['boot_sequence']['count']:
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.05)
        self.assertTrue(self.http.describe()['boot_sequence']['triggered'])
        self.assertTrue(self.http.network_status()['recovery'])
        self.restart()
        status = self.http.network_status()
        self.assertFalse(status['recovery'])
        self.assertEqual((status['mode'], status['http_port']), ('station', 8080))
        self.assertEqual(self.http.describe()['boot_sequence']['count'], 1)


if __name__ == '__main__':
    unittest.main()
