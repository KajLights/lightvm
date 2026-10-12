#!/usr/bin/env python3
"""Upload and execute independent WASM modules over USB CDC or a host pipe."""
import argparse
from dataclasses import dataclass
from pathlib import Path
import select
import struct
import subprocess
import sys
import time
import zlib

MAGIC = b"WASM"
HEADER = struct.Struct("<4sBBHII")
FRAME_LIMIT = 512
MODULE_LIMIT = 32768
STATUS, BEGIN, CHUNK, FINISH, RUN, RESET = range(1, 7)
OUTPUT, RESULT = 128, 129


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
    """The host executable runs the same handler and interpreter as the Pico."""
    def __init__(self, executable):
        self.process = subprocess.Popen([str(executable)], stdin=subprocess.PIPE,
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


class SerialTransport:
    def __init__(self, port):
        import serial
        self.serial = serial.Serial(port, 115200, timeout=0.1, write_timeout=3)
        # USB CDC uses DTR to indicate that a host is connected. Opening does
        # not reset this firmware, so repeated uploads use the same runtime.

    def write(self, data):
        self.serial.write(data)
        self.serial.flush()

    def read(self, size, timeout):
        self.serial.timeout = min(timeout, 0.1)
        return self.serial.read(size)

    def close(self):
        self.serial.close()


class Client:
    def __init__(self, transport, timeout=5, output=None):
        self.transport = transport
        self.timeout = timeout
        self.output = output or (lambda data: None)
        self.request_id = 0

    def read_exact(self, size, deadline):
        result = bytearray()
        while len(result) < size:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProtocolError("timed out waiting for receiver")
            result.extend(self.transport.read(size - len(result), remaining))
        return bytes(result)

    def command(self, kind, payload=b""):
        self.request_id = self.request_id % 65535 + 1
        request_id = self.request_id
        self.transport.write(frame(kind, request_id, payload))
        return self.receive_reply(request_id)

    def receive_reply(self, request_id):
        deadline = time.monotonic() + self.timeout
        while True:
            header = self.read_exact(HEADER.size, deadline)
            magic, version, kind, identity, size, checksum = HEADER.unpack(header)
            if magic != MAGIC or version != 1 or size > FRAME_LIMIT:
                raise ProtocolError("invalid response header")
            payload = self.read_exact(size, deadline)
            if zlib.crc32(payload, zlib.crc32(header[4:12])) != checksum:
                raise ProtocolError("response checksum mismatch")
            if identity != request_id:
                raise ProtocolError("unexpected response identity")
            if kind == OUTPUT:
                self.output(payload)
            elif kind == RESULT and len(payload) >= 8:
                code, value = struct.unpack("<Ii", payload[:8])
                return Reply(code, value, payload[8:].decode("utf-8", errors="replace"))
            else:
                raise ProtocolError("invalid response type or status payload")

    @staticmethod
    def require_ok(reply):
        if reply.code:
            raise ProtocolError(f"receiver status {reply.code}: {reply.message}")
        return reply

    def upload(self, bytecode):
        if not 8 <= len(bytecode) <= MODULE_LIMIT:
            raise ValueError(f"module must be 8..{MODULE_LIMIT} bytes")
        self.require_ok(self.command(BEGIN, struct.pack("<II", len(bytecode), zlib.crc32(bytecode))))
        for offset in range(0, len(bytecode), FRAME_LIMIT - 4):
            chunk = bytecode[offset:offset + FRAME_LIMIT - 4]
            reply = self.require_ok(self.command(CHUNK, struct.pack("<I", offset) + chunk))
            if reply.value != offset + len(chunk):
                raise ProtocolError("receiver acknowledged an unexpected upload offset")
        return self.require_ok(self.command(FINISH))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--port", help="USB serial port, e.g. /dev/cu.usbmodem...")
    transport.add_argument("--process", type=Path, help="desktop receiver executable")
    parser.add_argument("--function", default="run", help="zero-argument exported function")
    parser.add_argument("--status", action="store_true", help="query receiver without uploading")
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("module", nargs="?", type=Path)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if not args.status and args.module is None:
        parser.error("provide a .wasm module or --status")
    connection = None
    try:
        connection = SerialTransport(args.port) if args.port else ProcessTransport(args.process)
        def console(data):
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
        client = Client(connection, timeout=args.timeout, output=console)
        if args.module is not None:
            loaded = client.upload(args.module.read_bytes())
            print(f"Loaded {loaded.value} bytes", file=sys.stderr)
            reply = client.command(RUN, args.function.encode("utf-8"))
        else:
            reply = client.command(STATUS)
        print(f"Status {reply.code}, result {reply.value}: {reply.message}", file=sys.stderr)
        return 0 if reply.code == 0 else 1
    except (OSError, ValueError, ProtocolError, ImportError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    finally:
        if connection:
            connection.close()


if __name__ == "__main__":
    raise SystemExit(main())
