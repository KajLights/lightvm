"""Concurrent HTTP/pipe integration against the actual serialized engine."""
import concurrent.futures
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zlib
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from client import Client, HttpTransport, ProcessTransport, ProtocolError, frame, STATUS, BEGIN, OBJECT_BEGIN, OBJECT_CHUNK, OBJECT_FINISH, OBJECT_CANCEL

class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.receiver = Path(os.environ.get('LIGHTVM_HTTP_RECEIVER', os.environ.get('LIGHTVM_RECEIVER', ROOT / 'build/network-host/lightvm')))
        if not cls.receiver.exists():
            raise unittest.SkipTest('Build a host receiver with LIGHTVM_HTTP=ON')
        probe = ProcessTransport(cls.receiver)
        try:
            if not Client(probe).describe().get('http_supported'):
                raise unittest.SkipTest('HTTP excluded from this host build')
        finally:
            probe.close()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.storage = Path(self.directory.name) / 'store.bin'
        self.transports = []
        self.start()

    def start(self):
        self.process = subprocess.Popen([str(self.receiver), '--storage', str(self.storage), '--http', '0'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        line = self.process.stderr.readline().decode()
        self.assertIn('HTTP listening', line, 'Loopback sockets must be available for HTTP integration tests: ' + line)
        self.url = re.search(r'http://\S+', line)[0]
        self.pipe_transport = ProcessTransport.__new__(ProcessTransport)
        self.pipe_transport.process = self.process
        self.pipe = Client(self.pipe_transport)
        self.http = self.connect()

    def connect(self):
        transport = HttpTransport(self.url)
        self.transports.append(transport)
        return Client(transport)

    def stop(self):
        for transport in self.transports:
            transport.close()
        self.transports.clear()
        self.process.terminate()
        self.process.wait(timeout=5)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()

    def tearDown(self):
        self.stop()
        self.directory.cleanup()

    def config(self):
        return {'schema': 'lightvm.config/1', 'devices': [{'id': 'lights', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': 5}}], 'targets': {'A': {'device': 'lights'}}}

    def network(self):
        return {'schema': 'lightvm.network/1', 'wifi': {'enabled': False, 'country': 'XX', 'networks': [{'ssid': 'café🌈', 'password': 'secret123'}, {'ssid': 'open', 'password': '', 'hidden': True}]}, 'http': {'enabled': True, 'port': 8080}}

    def wait_network(self):
        deadline = time.monotonic() + 2
        while self.http.network_status()['state'] == 'reconfiguring':
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)

    def test_browser_preflight_has_no_command_body_or_session(self):
        from urllib.request import Request, urlopen
        request = Request(self.url + '/api/v1/command', method='OPTIONS', headers={'Origin': 'http://localhost:8000', 'Access-Control-Request-Method': 'POST', 'Access-Control-Request-Headers': 'content-type,x-lightvm-session'})
        with urlopen(request) as response:
            self.assertEqual(response.status, 204)
            self.assertEqual(response.headers['Access-Control-Allow-Origin'], '*')
            self.assertIn('X-LightVM-Session', response.headers['Access-Control-Allow-Headers'])
        self.assertEqual(self.pipe.status_fields()['uploading'], '0')

    def test_same_ids_and_identity_across_concurrent_paths(self):
        other = self.connect()
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            results = list(pool.map(lambda c: c.describe(), [self.http, other, self.pipe]))
        self.assertEqual(len({r['device_id'] for r in results}), 1)
        self.assertEqual(len({r['boot_id'] for r in results}), 1)
        self.http.configure(self.config(), save=True)
        self.assertEqual(self.pipe.get_config(), self.config())
        self.assertEqual(other.get_config(), self.config())

    def test_apply_and_save_cannot_persist_another_clients_configuration(self):
        original = self.config()
        self.http.configure(original, save=True)
        candidate = self.config()
        candidate['devices'][0]['config']['count'] = 6
        winner = self.config()
        winner['devices'][0]['config']['count'] = 7
        manage = self.http.manage

        def interleave(operation, **fields):
            reply = manage(operation, **fields)
            if operation == 'config_apply':
                self.pipe.configure(winner)
            return reply

        with patch.object(self.http, 'manage', side_effect=interleave):
            with self.assertRaisesRegex(ProtocolError, 'revision'):
                self.http.configure(candidate, save=True)
        self.assertEqual(self.pipe.get_config(), winner)
        self.assertEqual(self.pipe.get_config(saved=True), original)

    def test_upload_and_finished_candidate_are_session_owned(self):
        other = self.connect()
        data = json.dumps(self.config()).encode()
        self.http.require_ok(self.http.command(OBJECT_BEGIN, json.dumps({'kind': 'config', 'size': len(data), 'crc32': zlib.crc32(data)}).encode()))
        self.assertEqual(other.command(OBJECT_CANCEL).code, 9)
        self.assertEqual(self.pipe.command(BEGIN, struct.pack('<II', 8, 0)).code, 9)
        self.http.require_ok(self.http.command(OBJECT_CHUNK, struct.pack('<I', 0) + data))
        self.http.require_ok(self.http.command(OBJECT_FINISH))
        self.assertEqual(other.command(11, b'{"op":"config_apply","expected_revision":0}').code, 7)
        self.http.transport.close()
        other.configure(self.config())
        self.assertEqual(other.get_config(), self.config())

    def test_network_password_retention_reboot_and_eight_procedures(self):
        self.http.configure(self.config(), save=True)
        wasm = bytes.fromhex('0061736d01000000')
        for index in range(8):
            self.http.save_procedure('effect' + str(index), wasm)
        self.http.configure_network(self.network())
        self.wait_network()
        visible = self.http.network_config()
        self.assertNotIn('password', visible['wifi']['networks'][0])
        self.assertNotIn('secret123', json.dumps(visible))
        self.http.configure_network(visible)
        self.wait_network()
        self.assertEqual(len(self.pipe.procedures()), 8)
        before = self.http.describe()['device_id']
        self.stop()
        self.start()
        self.assertEqual(self.http.describe()['device_id'], before)
        self.assertEqual(self.http.network_config(), visible)
        self.assertEqual(len(self.http.procedures()), 8)
        self.assertEqual(self.http.describe()['network_revision'], 2)

    def test_existing_directory_migrates_without_losing_procedures(self):
        self.http.save_procedure('old', bytes.fromhex('0061736d01000000'))
        self.stop()
        data = bytearray(self.storage.read_bytes())
        for base in (0, 256 * 1024):
            if data[base:base + 4] != b'LVS1':
                continue
            struct.pack_into('<I', data, base + 4, 1)
            checksum = zlib.crc32(data[base:base + 1020])
            struct.pack_into('<I', data, base + 1020, checksum)
            struct.pack_into('<I', data, base + 1032, checksum)
            struct.pack_into('<I', data, base + 1036, zlib.crc32(data[base + 1024:base + 1036]))
        self.storage.write_bytes(data)
        self.start()
        self.assertEqual(self.http.procedures()[0]['name'], 'old')
        self.http.configure_network(self.network())
        self.assertEqual(self.http.procedures()[0]['name'], 'old')

    def test_large_download_preserves_all_frames(self):
        def leb(value):
            result = bytearray()
            while value >= 128:
                result.append((value & 127) | 128)
                value >>= 7
            return bytes(result + bytes([value]))
        payload = b'\x01x' + b'Z' * min(32000, self.http.describe()['module_limit'] - 64)
        wasm = bytes.fromhex('0061736d01000000') + b'\x00' + leb(len(payload)) + payload
        self.http.save_procedure('large', wasm)
        self.assertEqual(self.http.get_procedure('large'), wasm)
        self.assertEqual(self.pipe.get_procedure('large'), wasm)

    def test_stalled_download_keeps_pipe_and_animation_responsive(self):
        import wasmtime
        self.http.configure(self.config())
        prefix = bytes(wasmtime.wat2wasm('(module (func (export "render") (param i32)))'))
        payload = b'\x01x' + b'Z' * min(30000, self.http.describe()['module_limit'] - 128)
        value = len(payload)
        length = bytearray()
        while value >= 128:
            length.append((value & 127) | 128)
            value >>= 7
        length.append(value)
        self.http.save_procedure('large', prefix + b'\x00' + length + payload)
        self.http.activate('large')
        other = self.connect()
        port = int(self.url.rsplit(':', 1)[1])
        stalled = socket.socket()
        stalled.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        stalled.connect(('127.0.0.1', port))
        request = frame(11, 333, b'{"op":"procedure_get","name":"large"}')
        header = ('POST /api/v1/command HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/octet-stream\r\nX-LightVM-Session: ' + self.http.transport.session + '\r\nContent-Length: ' + str(len(request)) + '\r\n\r\n').encode()
        try:
            stalled.sendall(header + request)
            before = int(self.pipe.status_fields()['frames'])
            time.sleep(.1)
            started = time.monotonic()
            after = int(self.pipe.status_fields()['frames'])
            self.assertLess(time.monotonic() - started, .5)
            self.assertGreater(after, before)
            started = time.monotonic()
            self.assertEqual(other.status_fields()['mode'], 'wasm')
            self.assertLess(time.monotonic() - started, 3.5)
        finally:
            stalled.close()

    def test_invalid_http_frame_is_rejected_before_execution(self):
        bad = bytearray(frame(BEGIN, 1, struct.pack('<II', 8, 0)))
        bad[-1] ^= 1
        with self.assertRaises(ProtocolError):
            self.http.transport.write(bytes(bad))
        self.assertEqual(self.pipe.status_fields()['uploading'], '0')
        with self.assertRaises(ProtocolError):
            self.http.transport.write(frame(STATUS, 1) + frame(STATUS, 2))
        self.assertEqual(self.http.status_fields()['mode'], 'idle')

    def test_four_sessions_and_explicit_release(self):
        clients = [self.connect() for _ in range(3)]
        with self.assertRaises(ProtocolError):
            self.connect()
        clients[0].transport.close()
        replacement = self.connect()
        self.assertNotEqual(replacement.transport.session, clients[1].transport.session)
        self.assertEqual(replacement.status_fields()['mode'], 'idle')

    def test_control_parameters_and_conflicts_work_on_both_paths(self):
        from lightvm.compiler import compile_source
        import wasmtime
        source = 'from lightvm import parameter, on_frame, rgb\nspeed = parameter("speed", 1.0)\n@on_frame\ndef draw(ctx):\n    ctx.pixels[0] = rgb(17, 34, 51)\n'
        compiled = compile_source(source)
        self.assertFalse(compiled.diagnostics)
        wasm = compiled.wasm
        self.http.configure(self.config())
        self.http.save_procedure('control', wasm)
        self.pipe.activate('control')
        old = self.http.program_parameters()
        self.pipe.set_parameters({'speed': 2.0})
        with self.assertRaises(ProtocolError):
            self.http.manage('program_parameters_set', values={'speed': 3.0}, expected_generation=old['generation'], expected_parameter_revision=old['parameter_revision'])
        self.assertEqual(self.http.program_parameters()['values']['speed'], 2.0)
        self.http.set_parameters({'speed': 4.0})
        self.assertEqual(self.pipe.program_parameters()['values']['speed'], 4.0)
        self.assertEqual(self.http.snapshot()[1][:3], bytes.fromhex('112233'))

if __name__ == '__main__':
    unittest.main()
