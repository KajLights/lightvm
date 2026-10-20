"""The board's plain HTML form against the real HTTP and persistence engine."""
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import unittest
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import build_opener, ProxyHandler, Request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from client import Client, ProcessTransport, OBJECT_CANCEL


class FormParser(HTMLParser):
    def __init__(self, html):
        super().__init__()
        self.form = {}
        self.inputs = {}
        self.feed(html)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == 'form':
            self.form = attrs
        if tag == 'input':
            self.inputs[attrs['name']] = attrs


class WifiSetupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.receiver = Path(os.environ.get('LIGHTVM_SETUP_RECEIVER', os.environ.get(
            'LIGHTVM_RECEIVER', ROOT / 'build/power-recovery-host/lightvm')))
        if not cls.receiver.exists():
            raise unittest.SkipTest('Build a host receiver with Wi-Fi and HTTP enabled')
        probe = ProcessTransport(cls.receiver)
        try:
            info = Client(probe).describe()
            if not (info.get('wifi_supported') and info.get('http_supported')):
                raise unittest.SkipTest('Wi-Fi setup excluded from this host build')
        finally:
            probe.close()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.storage = Path(self.directory.name) / 'store.bin'
        self.opener = build_opener(ProxyHandler({}))
        self.process = None
        self.start()

    def start(self):
        self.process = subprocess.Popen([str(self.receiver), '--storage', str(self.storage), '--http', '0'],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, bufsize=0)
        line = self.process.stderr.readline().decode()
        self.assertIn('HTTP listening', line)
        self.url = re.search(r'http://\S+', line)[0]
        pipe = ProcessTransport.__new__(ProcessTransport)
        pipe.process = self.process
        self.client = Client(pipe)

    def stop(self):
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

    def request(self, path='/', data=None, origin=None, content_type='application/x-www-form-urlencoded'):
        headers = {}
        if data is not None:
            headers = {'Content-Type': content_type, 'Origin': origin or self.url}
        request = Request(self.url + path, data=data, headers=headers)
        try:
            response = self.opener.open(request, timeout=5)
        except HTTPError as error:
            response = error
        with response:
            return response.status, response.headers, response.read().decode()

    def form(self, **changes):
        code, headers, html = self.request()
        self.assertEqual(code, 200)
        parsed = FormParser(html)
        data = {k: v.get('value', '') for k, v in parsed.inputs.items() if v.get('type') != 'checkbox'}
        data.update(ssid='studio', password='station-secret', country='it')
        data.update(changes)
        return data

    def post(self, data=None, **kwargs):
        return self.request('/setup', urlencode(data or self.form()).encode(), **kwargs)

    def settled(self):
        deadline = time.monotonic() + 2
        while self.client.network_status()['state'] == 'reconfiguring':
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)

    def profile(self, count=2):
        return {'schema': 'lightvm.network', 'wifi': {
            'enabled': True, 'mode': 'ap', 'country': 'XX',
            'ap': {'ssid': 'private-lights', 'password': 'saved-ap-secret'},
            'networks': [{'ssid': f'network-{i}', 'password': f'station-secret-{i}'} for i in range(count)],
        }, 'http': {'enabled': True, 'port': 8080}}

    def save_profile(self, count=2):
        profile = self.profile(count)
        if self.client.describe().get('mqtt_supported'):
            profile['mqtt'] = {'enabled': True, 'host': 'broker.local', 'port': 1883,
                               'username': 'lightvm', 'password': 'mqtt-secret'}
        self.client.configure_network(profile)
        self.settled()

    def test_plain_local_form_and_persisted_post(self):
        code, headers, html = self.request()
        self.assertEqual(code, 200)
        self.assertEqual(headers.get_content_type(), 'text/html')
        self.assertEqual(headers['Cache-Control'], 'no-store')
        self.assertEqual(FormParser(html).form, {'method': 'post', 'action': '/setup'})
        self.assertNotRegex(html.lower(), r'<script|<link|<style|https?://|\son\w+=')
        code, headers, html = self.post()
        self.assertEqual(code, 200)
        self.assertIn('Wi-Fi saved', html)
        self.assertNotIn('station-secret', html)
        self.settled()
        config = self.client.network_config()
        self.assertEqual(config['wifi']['mode'], 'station')
        self.assertEqual(config['wifi']['country'], 'IT')
        self.assertTrue(config['wifi']['enabled'])
        self.assertTrue(config['http']['enabled'])
        self.assertEqual(config['wifi']['networks'][0]['ssid'], 'studio')
        self.assertIn(b'station-secret', self.storage.read_bytes())
        self.restart()
        self.assertEqual(self.client.network_config(), config)

    def test_url_encoding_and_html_escaping_without_password_echo(self):
        ssid = 'Café & <lights> +\'"'
        password = 'secret +&%<>"\''
        self.assertEqual(self.post(self.form(ssid=ssid, password=password))[0], 200)
        self.settled()
        self.assertEqual(self.client.network_config()['wifi']['networks'][0]['ssid'], ssid)
        code, _, html = self.request()
        self.assertEqual(code, 200)
        self.assertEqual(FormParser(html).inputs['ssid']['value'], ssid)
        self.assertNotIn('<lights>', html)
        self.assertNotIn(password, html)
        self.assertNotIn('value', FormParser(html).inputs['password'])
        encoded = json.dumps(password, ensure_ascii=False).encode()
        self.assertTrue(encoded in self.storage.read_bytes(), 'URL-decoded password was not saved correctly')

    def test_preserves_other_networks_services_and_secrets(self):
        self.save_profile()
        before = self.client.network_config()
        data = self.form(ssid='network-1', password='', hidden='1')
        self.assertEqual(self.post(data)[0], 200)
        self.settled()
        after = self.client.network_config()
        self.assertEqual([n['ssid'] for n in after['wifi']['networks']], ['network-1', 'network-0'])
        self.assertTrue(after['wifi']['networks'][0]['hidden'])
        self.assertEqual(after['wifi']['ap'], before['wifi']['ap'])
        self.assertEqual(after['http']['port'], 8080)
        self.assertEqual(after['mqtt'], before['mqtt'])
        if before['mqtt']['enabled']:
            self.assertTrue(b'mqtt-secret' in self.storage.read_bytes(), 'Saved MQTT password was lost')
        for secret in (b'station-secret-0', b'station-secret-1', b'saved-ap-secret'):
            self.assertIn(secret, self.storage.read_bytes())

    def test_open_network_must_be_explicit(self):
        data = self.form(password='')
        self.assertEqual(self.post(data)[0], 400)
        data['open'] = '1'
        self.assertEqual(self.post(data)[0], 200)
        self.settled()
        self.assertEqual(self.client.network_config()['wifi']['networks'][0]['ssid'], 'studio')
        self.assertNotIn(b'station-secret', self.storage.read_bytes())

    def test_invalid_fields_do_not_write_or_leave_setup(self):
        snapshot = self.storage.read_bytes()
        config = self.client.network_config()
        for change in ({'password': 'short'}, {'country': '1T'}, {'ssid': ''},
                       {'ssid': 'é' * 17}, {'password': 'x' * 64}):
            with self.subTest(change=change):
                code, headers, html = self.post(self.form(**change))
                self.assertEqual(code, 400)
                self.assertEqual(headers.get_content_type(), 'text/html')
                self.assertIn('<form ', html)
                self.assertEqual(self.storage.read_bytes(), snapshot)
                self.assertEqual(self.client.network_config(), config)

    def test_cross_site_expired_and_stale_forms_do_not_write(self):
        data = self.form()
        snapshot = self.storage.read_bytes()
        self.assertEqual(self.post(data, origin='http://another-site.example')[0], 403)
        self.assertEqual(self.post({**data, 'token': 'expired'})[0], 403)
        self.assertEqual(self.post({**data, 'revision': '1'})[0], 409)
        self.assertEqual(self.storage.read_bytes(), snapshot)
        self.assertEqual(self.post(data)[0], 200)
        self.settled()
        snapshot = self.storage.read_bytes()
        self.assertEqual(self.post(data)[0], 409)
        self.assertEqual(self.storage.read_bytes(), snapshot)

    def test_malformed_forms_and_media_types(self):
        valid = urlencode(self.form()).encode()
        snapshot = self.storage.read_bytes()
        for suffix in (b'&ssid=duplicate', b'&unknown=value', b'&open=0', b'&hidden=%00', b'&hidden=%QQ'):
            with self.subTest(suffix=suffix):
                self.assertEqual(self.request('/setup', valid + suffix)[0], 400)
        for ssid in ('%FF', '%C0%AF', '%ED%A0%80', '%F4%90%80%80', '%E2%82'):
            body = valid.replace(b'ssid=studio', ('ssid=' + ssid).encode())
            self.assertEqual(self.request('/setup', body)[0], 400)
        self.assertEqual(self.request('/setup', b'x' * 529)[0], 413)
        self.assertEqual(self.post(content_type='application/json')[0], 415)
        self.assertEqual(self.request('/setup')[0], 405)
        self.assertEqual(self.storage.read_bytes(), snapshot)

    def test_form_cannot_take_another_sessions_upload(self):
        self.client.upload_object('network', json.dumps(self.profile()).encode())
        self.assertEqual(self.post()[0], 409)
        # The candidate is still owned by the pipe session and can be validated.
        self.client.manage('network_validate')
        self.client.require_ok(self.client.command(OBJECT_CANCEL))
        self.assertEqual(self.post()[0], 200)

    def test_eight_network_limit_allows_existing_network_update(self):
        self.save_profile(8)
        snapshot = self.storage.read_bytes()
        self.assertEqual(self.post()[0], 409)
        self.assertEqual(self.storage.read_bytes(), snapshot)
        self.assertEqual(self.post(self.form(ssid='network-7', password=''))[0], 200)
        self.settled()
        networks = self.client.network_config()['wifi']['networks']
        self.assertEqual(len(networks), 8)
        self.assertEqual(networks[0]['ssid'], 'network-7')

    def test_recovery_save_keeps_ap_until_restart(self):
        if not self.client.describe().get('recovery_supported'):
            self.skipTest('Power recovery excluded from this build')
        self.save_profile()
        self.restart()
        self.restart()
        self.assertTrue(self.client.network_status()['recovery'])
        code, _, html = self.post()
        self.assertEqual(code, 200)
        self.assertIn('Restart the board', html)
        self.assertTrue(self.client.network_status()['recovery'])
        self.assertEqual(self.client.network_status()['mode'], 'ap')
        self.assertEqual(self.client.network_config()['wifi']['networks'][0]['ssid'], 'studio')
        self.restart()
        self.assertEqual(self.client.network_config()['wifi']['networks'][0]['ssid'], 'studio')


if __name__ == '__main__':
    unittest.main()
