"""RFC 6455 wire and concurrent LightVM integration; standard library only."""
import base64
import concurrent.futures
import hashlib
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
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from client import Client, HttpTransport, ProcessTransport, ProtocolError, frame, STATUS, BEGIN
from bytecode_fixtures import color, padded_program


def masked(payload, opcode=2, final=True):
    key = b"test"
    size = len(payload)
    head = bytes([(0x80 if final else 0) | opcode])
    head += bytes([0x80 | size]) if size < 126 else b"\xfe" + struct.pack("!H", size)
    return head + key + bytes(value ^ key[i % 4] for i, value in enumerate(payload))


class Wire:
    def __init__(self, url):
        address = urlsplit(url)
        self.socket = socket.create_connection((address.hostname, address.port), timeout=3)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buffer = bytearray()
        key = base64.b64encode(os.urandom(16)).decode()
        response = self.handshake(key)
        expected = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
        if b"101 Switching Protocols" not in response or b"Sec-WebSocket-Accept: " + expected not in response:
            self.socket.close()
            raise AssertionError(response)

    def handshake(self, key, extra="", version="13", path="/api/v1/ws"):
        request = (f"GET {path} HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
                   f"Connection: keep-alive, Upgrade\r\nSec-WebSocket-Version: {version}\r\n"
                   f"Sec-WebSocket-Key: {key}\r\n{extra}\r\n").encode()
        self.socket.sendall(request)
        response = bytearray()
        while not response.endswith(b"\r\n\r\n"):
            part = self.socket.recv(1)
            if not part:
                raise AssertionError(response)
            response.extend(part)
        return bytes(response)

    def exact(self, size):
        result = bytearray()
        while len(result) < size:
            part = self.socket.recv(size - len(result))
            if not part:
                raise ProtocolError("WebSocket closed")
            result.extend(part)
        return bytes(result)

    def message(self):
        first, second = self.exact(2)
        if not first & 0x80 or second & 0x80:
            raise AssertionError("server frames must be final and unmasked")
        size = second & 127
        if size == 126:
            size = struct.unpack("!H", self.exact(2))[0]
        elif size == 127:
            size = struct.unpack("!Q", self.exact(8))[0]
        return first & 15, self.exact(size)

    def write(self, data):
        self.socket.sendall(masked(data))

    def read(self, size, timeout):
        if not self.buffer:
            self.socket.settimeout(timeout)
            opcode, payload = self.message()
            if opcode != 2:
                raise ProtocolError(f"unexpected WebSocket opcode {opcode}")
            self.buffer.extend(payload)
        result = bytes(self.buffer[:size])
        del self.buffer[:size]
        return result

    def close(self):
        self.socket.close()


class WebSocketTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.receiver = Path(os.environ.get("LIGHTVM_WEBSOCKET_RECEIVER", os.environ.get("LIGHTVM_RECEIVER", ROOT / "build/ws-host/lightvm")))
        if not cls.receiver.exists():
            raise unittest.SkipTest("Build with LIGHTVM_HTTP=ON and LIGHTVM_WEBSOCKET=ON")
        transport = ProcessTransport(cls.receiver)
        try:
            if not Client(transport).describe().get("websocket_supported"):
                raise unittest.SkipTest("WebSocket module excluded from this build")
        finally:
            transport.close()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.process = subprocess.Popen([str(self.receiver), "--storage", str(Path(self.directory.name) / "store.bin"), "--http", "0"],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        line = self.process.stderr.readline().decode()
        self.assertIn("HTTP listening", line)
        self.url = re.search(r"http://\S+", line)[0]
        self.transports = []
        transport = ProcessTransport.__new__(ProcessTransport)
        transport.process = self.process
        self.pipe = Client(transport)

    def tearDown(self):
        for transport in self.transports:
            transport.close()
        self.process.terminate()
        self.process.wait(timeout=5)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()
        self.directory.cleanup()

    def connect(self):
        wire = Wire(self.url)
        self.transports.append(wire)
        return wire

    def test_persistent_commands_concurrent_with_http_and_pipe(self):
        first, second = Client(self.connect()), Client(self.connect())
        http = HttpTransport(self.url)
        self.transports.append(http)
        clients = [first, second, Client(http), self.pipe]
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
            results = list(executor.map(lambda c: [c.describe()["controller_id"] for _ in range(15)], clients))
        self.assertEqual(len({identity for values in results for identity in values}), 1)

    def test_fragmentation_ping_and_partial_tcp_reads(self):
        wire = self.connect()
        command = frame(STATUS, 42, b"")
        packet = masked(command[:7], final=False) + masked(b"hello", opcode=9) + masked(command[7:], opcode=0)
        for byte in packet:
            wire.socket.sendall(bytes([byte]))
        self.assertEqual(wire.message(), (10, b"hello"))
        opcode, reply = wire.message()
        self.assertEqual(opcode, 2)
        self.assertEqual(reply[:4], b"LVM1")
        self.assertIn(b"board=", reply)
        self.assertEqual(Client(wire).describe()["websocket_supported"], True)

    def test_multiple_messages_in_one_tcp_write(self):
        wire = self.connect()
        wire.socket.sendall(b"".join(masked(frame(STATUS, i, b"")) for i in range(1, 4)))
        for i in range(1, 4):
            opcode, reply = wire.message()
            self.assertEqual(opcode, 2)
            self.assertEqual(struct.unpack_from("<H", reply, 6)[0], i)

    def test_large_response_and_upload(self):
        client = Client(self.connect())
        program = padded_program(min(20000, client.describe()["module_limit"]))
        client.save_program("large", program)
        self.assertEqual(client.get_program("large"), program)
        self.assertEqual(self.pipe.get_program("large"), program)

    def test_disconnect_releases_upload_ownership(self):
        wire = self.connect()
        client = Client(wire)
        program = color(20, 30, 40)
        client.configure({"schema": "lightvm.config", "outputs": [{"id": "lights", "type": "ws2812b", "config": {"pin": "GP16", "count": 5}}]})
        reply = client.command(BEGIN, struct.pack("<II", len(program), zlib.crc32(program)))
        self.assertEqual(reply.code, 0)
        self.assertEqual(self.pipe.status_fields()["uploading"], "1")
        wire.close()
        deadline = time.monotonic() + 2
        while self.pipe.status_fields()["uploading"] != "0":
            self.assertLess(time.monotonic(), deadline)
        self.assertTrue(Client(self.connect()).describe()["websocket_supported"])

    def test_close_handshake_and_idle_connection(self):
        wire = self.connect()
        time.sleep(2.05)
        self.assertTrue(Client(wire).describe()["websocket_supported"])
        payload = struct.pack("!H", 1000) + b"done"
        wire.socket.sendall(masked(payload, opcode=8))
        self.assertEqual(wire.message(), (8, payload))
        self.assertEqual(wire.socket.recv(1), b"")

    def test_invalid_frames_close_without_affecting_other_transports(self):
        cases = [
            (b"\x82\x00", 1002),
            (masked(b"text", opcode=1), 1003),
            (b"\xc2" + masked(b"x")[1:], 1002),
            (masked(b"x", opcode=0), 1002),
            (masked(b"x", opcode=3), 1002),
            (masked(b"x" * 529), 1009),
            (masked(b"x" * 126, opcode=9), 1002),
            (masked(b"x", opcode=9, final=False), 1002),
            (masked(b"x", opcode=8), 1002),
            (masked(struct.pack("!H", 1005), opcode=8), 1002),
            (masked(struct.pack("!H", 1000) + b"\xff", opcode=8), 1007),
            (masked(b"invalid protocol frame"), 1002),
            (masked(b"first", final=False) + masked(b"second"), 1002),
            (b"\x82\xfe\x00\x01test" + b"x", 1002),
            (b"\x82\xff" + struct.pack("!Q", 65536) + b"test", 1009),
        ]
        for packet, expected in cases:
            with self.subTest(expected=expected, packet=packet[:14]):
                wire = self.connect()
                wire.socket.settimeout(1)
                wire.socket.sendall(packet)
                opcode, payload = wire.message()
                self.assertEqual(opcode, 8)
                self.assertEqual(struct.unpack_from("!H", payload)[0], expected)
                self.assertEqual(self.pipe.status_fields()["uploading"], "0")

    def test_handshake_validation(self):
        address = urlsplit(self.url)
        cases = [
            ("not-base64", "", "13", "/api/v1/ws", 400),
            ("dGhlIHNhbXBsZSBub25jZQ==", "Sec-WebSocket-Key: duplicate\r\n", "13", "/api/v1/ws", 400),
            ("dGhlIHNhbXBsZSBub25jZQ==", "", "12", "/api/v1/ws", 426),
            ("dGhlIHNhbXBsZSBub25jZQ==", "", "13", "/api/v1/missing", 404),
            ("dGhlIHNhbXBsZSBub25jZQ==", "Content-Length: 1\r\n", "13", "/api/v1/ws", 400),
        ]
        for key, extra, version, path, expected in cases:
            with self.subTest(expected=expected):
                wire = Wire.__new__(Wire)
                wire.socket = socket.create_connection((address.hostname, address.port), timeout=3)
                self.transports.append(wire)
                if "Content-Length" in extra:
                    # An upgrade with a body must be rejected after the complete request.
                    wire.socket.sendall((f"GET {path} HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\n{extra}\r\nx").encode())
                    response = wire.socket.recv(4096)
                else:
                    response = wire.handshake(key, extra, version, path)
                self.assertIn(f"HTTP/1.1 {expected}".encode(), response)
                if expected == 426:
                    self.assertIn(b"Sec-WebSocket-Version: 13", response)
                    self.assertIn(b"Upgrade: websocket", response)


if __name__ == "__main__":
    unittest.main()
