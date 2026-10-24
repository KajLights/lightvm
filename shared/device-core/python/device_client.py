#!/usr/bin/env python3
"""Common framed device management over USB, HTTP or a host pipe."""
import json
from dataclasses import dataclass
import select
import struct
import subprocess
import time
import zlib

MAGIC = b"LVM1"
HEADER = struct.Struct("<4sBBHII")
FRAME_LIMIT = 512
MODULE_LIMIT = 32768
STATUS, BEGIN, CHUNK, FINISH, RESET = 1, 2, 3, 4, 6
PLAY, STOP, FRAME = 7, 8, 9
MANAGE, OBJECT_BEGIN, OBJECT_CHUNK, OBJECT_FINISH, OBJECT_CANCEL = range(11, 16)
RESULT, PIXELS, DATA = 129, 130, 131


class ProtocolError(RuntimeError):
    pass


@dataclass
class Reply:
    code: int
    value: int
    message: str


def frame(kind, request_id, payload=b""):
    if len(payload) > FRAME_LIMIT:
        raise ValueError("payload exceeds frame limit")
    metadata = struct.pack("<BBHI", 1, kind, request_id, len(payload))
    checksum = zlib.crc32(payload, zlib.crc32(metadata))
    return MAGIC + metadata + struct.pack("<I", checksum) + payload


class ProcessTransport:
    """Native service validation transport."""
    def __init__(self, executable, storage=None):
        argv = [str(executable)]
        if storage is not None:
            argv += ["--storage", str(storage)]
        self.process = subprocess.Popen(argv, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, bufsize=0)

    def write(self, data):
        view = memoryview(data)
        while view:
            written = self.process.stdin.write(view)
            if not written:
                raise ProtocolError("receiver closed its input")
            view = view[written:]

    def read(self, size, timeout):
        if not select.select([self.process.stdout], [], [], timeout)[0]:
            return b""
        data = self.process.stdout.read(size)
        if not data:
            raise ProtocolError("receiver closed its output")
        return data

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.process.stdout.close()


def _keep_usb_connected(port):
    # POSIX hangup-on-close also changes modem lines on native USB Serial/JTAG.
    import os
    if os.name == "posix":
        import termios
        settings = termios.tcgetattr(port.fileno())
        settings[2] &= ~termios.HUPCL
        termios.tcsetattr(port.fileno(), termios.TCSANOW, settings)


class SerialTransport:
    def __init__(self, port, no_reset=False):
        import serial
        if no_reset:
            # Native ESP32 USB uses modem-line transitions as reset/download
            # requests. PySerial updates DTR then RTS separately during open;
            # even setting both False causes a transient reset on macOS.
            # Suppress those two driver hooks only in explicit no-reset mode.
            self.serial = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=3)
            self.serial.dtr = False
            self.serial.rts = False
            self.serial._update_dtr_state = lambda: None
            self.serial._update_rts_state = lambda: None
            self.serial.port = port
            try:
                self.serial.open()
                _keep_usb_connected(self.serial)
                # A fresh connection can contain ROM boot text or a reply from
                # an interrupted request. Nothing is requested yet: drain until
                # quiet, with a bound, before parsing this connection's frames.
                deadline = time.monotonic() + 0.5
                while True:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        break
                    self.serial.timeout = min(0.1, remaining)
                    if not self.serial.read(4096):
                        break
                self.serial.timeout = 0.1
            except Exception:
                self.serial.close()
                raise
        else:
            # Preserve Pico CDC's existing DTR host-connection behavior.
            self.serial = serial.Serial(port, 115200, timeout=0.1, write_timeout=3)

    def write(self, data):
        self.serial.write(data)
        self.serial.flush()

    def read(self, size, timeout):
        self.serial.timeout = min(timeout, 0.1)
        return self.serial.read(size)

    def close(self):
        self.serial.close()


class HttpTransport:
    """One logical session across independent HTTP exchanges; never replay commands."""
    def __init__(self, url, timeout=5):
        from urllib.parse import urlsplit
        from urllib.request import build_opener, ProxyHandler
        parsed = urlsplit(url)
        if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment or parsed.path not in ("", "/"):
            raise ValueError("--url must be a device HTTP origin, e.g. http://192.168.1.20")
        self.url = url.rstrip("/")
        self.timeout = timeout
        self.opener = build_opener(ProxyHandler({}))
        self.buffer = bytearray()
        self.session = None
        self.identity = json.loads(self.exchange("/api/v1/sessions", b""))
        self.session = self.identity["session_id"]

    def exchange(self, path, data=None, method="POST"):
        from urllib.request import Request
        from urllib.error import URLError
        from http.client import HTTPException
        headers = {"Content-Type": "application/octet-stream"}
        if self.session:
            headers["X-LightVM-Session"] = self.session
        try:
            with self.opener.open(Request(self.url + path, data=data, headers=headers, method=method), timeout=self.timeout) as response:
                result = response.read(4 * 1024 * 1024 + 1)
                if len(result) > 4 * 1024 * 1024:
                    raise ProtocolError("HTTP response exceeds client limit")
                return result
        except (URLError, OSError, HTTPException) as error:
            raise ProtocolError(f"HTTP request failed: {error}; reconnect and inspect state before retrying") from error

    def write(self, data):
        self.buffer = bytearray(self.exchange("/api/v1/command", data))

    def read(self, size, timeout):
        result = bytes(self.buffer[:size])
        del self.buffer[:size]
        return result

    def close(self):
        if self.session:
            try:
                self.exchange("/api/v1/sessions/" + self.session, method="DELETE")
            except ProtocolError:
                pass
            self.session = None


class DeviceClient:
    def __init__(self, transport, timeout=5):
        self.transport = transport
        self.timeout = timeout
        self.request_id = 0
        self.data_reply = bytearray()

    def exact(self, size, deadline):
        result = bytearray()
        while len(result) < size:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProtocolError("timed out waiting for device")
            result.extend(self.transport.read(size - len(result), remaining))
        return bytes(result)

    def command(self, kind, payload=b""):
        self.request_id = self.request_id % 65535 + 1
        self.data_reply.clear()
        self.transport.write(frame(kind, self.request_id, payload))
        deadline = time.monotonic() + self.timeout
        while True:
            # USB may carry boot/runtime diagnostics between complete frames.
            magic = bytearray(self.exact(4, deadline))
            skipped = 0
            while magic != MAGIC:
                magic.pop(0)
                magic.extend(self.exact(1, deadline))
                skipped += 1
                if skipped > 65536:
                    raise ProtocolError("too much non-protocol USB output")
            header = bytes(magic) + self.exact(HEADER.size - 4, deadline)
            _, version, response, identity, size, checksum = HEADER.unpack(header)
            if version != 1 or size > FRAME_LIMIT or identity != self.request_id:
                raise ProtocolError("invalid response header")
            data = self.exact(size, deadline)
            if zlib.crc32(data, zlib.crc32(header[4:12])) != checksum:
                raise ProtocolError("response checksum mismatch")
            if response == DATA:
                self.data_reply.extend(data)
                if len(self.data_reply) > 8192:
                    raise ProtocolError("device response exceeds limit")
            elif response == RESULT and len(data) >= 8:
                code, value = struct.unpack("<Ii", data[:8])
                return Reply(code, value, data[8:].decode("utf-8", errors="replace"))
            else:
                raise ProtocolError("invalid response type")

    @staticmethod
    def require_ok(reply):
        if reply.code:
            raise ProtocolError(f"device status {reply.code}: {reply.message}")
        return reply

    def manage(self, operation, **fields):
        payload = json.dumps({"op": operation, **fields}, separators=(",", ":"), allow_nan=False).encode()
        return self.require_ok(self.command(MANAGE, payload))

    def document(self, operation):
        self.manage(operation)
        return json.loads(self.data_reply)

    def upload_object(self, kind, data):
        if kind not in ("config", "network") or not 1 <= len(data) <= 4096:
            raise ValueError("invalid settings object")
        metadata = json.dumps({"kind": kind, "size": len(data), "crc32": zlib.crc32(data)}).encode()
        self.require_ok(self.command(OBJECT_BEGIN, metadata))
        for offset in range(0, len(data), FRAME_LIMIT - 4):
            chunk = data[offset:offset + FRAME_LIMIT - 4]
            reply = self.require_ok(self.command(OBJECT_CHUNK, struct.pack("<I", offset) + chunk))
            if reply.value != offset + len(chunk):
                raise ProtocolError("unexpected object offset")
        self.require_ok(self.command(OBJECT_FINISH))
