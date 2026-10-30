"""Shared device contracts and durable nicknames against the actual native service."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from client import Client, ProcessTransport, ProtocolError, MANAGE
from test_configuration import config


class DeviceMetadataTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.storage = Path(self.temp.name) / 'device.bin'
        self.receiver = Path(os.environ.get('LIGHTVM_RECEIVER', ROOT / 'build/host/lightvm'))
        self.open()

    def open(self):
        self.transport = ProcessTransport(self.receiver, self.storage)
        self.client = Client(self.transport)

    def tearDown(self):
        self.transport.close()
        self.temp.cleanup()

    def schema(self, name, value):
        schema = json.loads((ROOT / 'shared/device-core/contracts' / name).read_text())
        Draft202012Validator.check_schema(schema)
        Draft202012Validator(schema).validate(value)

    def test_description_matches_machine_contract_and_real_features(self):
        value = self.client.describe()
        self.schema('device.schema.json', value)
        self.assertEqual(value['product'], 'lightvm')
        self.assertEqual(value['hardware']['target'], value['board'])
        self.assertFalse(value['updates']['supported'])
        self.assertNotIn('ota', value['features'])
        self.assertIn('device_metadata', value['features'])
        self.assertEqual(value['limits']['module_bytes'], value['module_limit'])
        self.assertEqual('mqtt' in value['features'], value['mqtt_supported'])
        self.assertEqual('audio_subscriptions' in value['features'], value['audio_subscriptions_supported'])
        canonical = json.dumps(value['features'], sort_keys=True, separators=(',', ':')).encode()
        self.assertEqual(value['firmware']['features_hash'], 'sha256:' + hashlib.sha256(canonical).hexdigest())
        self.assertEqual(value['features'], sorted(set(value['features'])))

    def test_name_is_normalized_saved_and_shared_with_other_client(self):
        first = self.client.describe()
        self.assertIsNone(first['nickname'])
        result = self.client.set_device_nickname('  Kitchen "shelf" \\ café 🟡  ', first)
        self.schema('metadata.schema.json', result)
        self.assertEqual(result['nickname'], 'Kitchen "shelf" \\ café 🟡')
        self.assertGreater(result['revision'], first['metadata_revision'])
        current = self.client.describe()
        self.assertEqual(current['controller_id'], first['controller_id'])
        self.assertEqual(current['boot_id'], first['boot_id'])
        self.assertEqual(current['active_revision'], first['active_revision'])
        self.assertEqual(current['network_revision'], first['network_revision'])
        self.transport.close()
        self.open()
        restarted = self.client.describe()
        self.assertNotEqual(restarted['boot_id'], first['boot_id'])
        self.assertEqual(restarted['controller_id'], first['controller_id'])
        self.assertEqual(restarted['nickname'], result['nickname'])
        sys.path.insert(0, str(ROOT / 'shared/device-core/python'))
        from device_client import DeviceClient, ProcessTransport as SharedTransport
        self.transport.close()
        other_transport = SharedTransport(self.receiver, self.storage)
        try:
            other = DeviceClient(other_transport)
            self.assertEqual(other.document('device_metadata_get')['nickname'], result['nickname'])
            self.assertIsNone(other.set_device_nickname(None)['nickname'])
        finally:
            other_transport.close()
        self.open()
        self.assertIsNone(self.client.describe()['nickname'])

    def test_stale_revision_and_boot_are_rejected_without_changing_name(self):
        before = self.client.describe()
        self.client.set_device_nickname('First', before)
        with self.assertRaises(ProtocolError):
            self.client.set_device_nickname('Stale', before)
        current = self.client.describe()
        with self.assertRaises(ProtocolError):
            self.client.set_device_nickname('Wrong boot', {**current, 'boot_id': 'another-boot'})
        self.assertEqual(self.client.describe()['nickname'], 'First')
        self.client.configure(config(), save=True)
        self.assertEqual(self.client.describe()['metadata_revision'], current['metadata_revision'])
        self.client.set_device_nickname('After configuration', current)

    def test_utf8_limits_controls_and_clearing(self):
        for name, expected in [('null', 'null'), ('é' * 31 + 'x', 'é' * 31 + 'x'), (' ' * 10 + 'A' * 63 + ' ' * 10, 'A' * 63), ('   ', None), (None, None)]:
            with self.subTest(name=name):
                result = self.client.set_device_nickname(name)
                self.assertEqual(result['nickname'], expected)
        before = self.client.describe()
        for name in ['é' * 32, 'A' * 64, 'line\nfeed', '\x00', '\x7f', '\ud800', [], False, 2]:
            with self.subTest(name=repr(name)), self.assertRaises(ProtocolError):
                self.client.set_device_nickname(name)
        after = self.client.describe()
        self.assertEqual(after['nickname'], before['nickname'])
        self.assertEqual(after['metadata_revision'], before['metadata_revision'])
        payload = b'{"op":"device_metadata_set","nickname":"\xc0\x80","expected_revision":0,"expected_boot_id":"x"}'
        self.assertNotEqual(self.client.command(MANAGE, payload).code, 0)

    def test_read_rejects_unknown_fields_and_mutation_is_guarded(self):
        with self.assertRaises(ProtocolError):
            self.client.manage('device_metadata_get', nickname='unexpected')
        for fields in [{}, {'nickname': 'Unguarded'}, {'nickname': 'X', 'expected_revision': 0}]:
            with self.subTest(fields=fields), self.assertRaises(ProtocolError):
                self.client.manage('device_metadata_set', **fields)


if __name__ == '__main__':
    unittest.main()
