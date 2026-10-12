#!/usr/bin/env python3
"""Upload and execute independent WASM modules over USB CDC or a host pipe."""
import argparse
import json
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
PLAY, STOP, FRAME, NATIVE_DEMO = 7, 8, 9, 10
MANAGE, OBJECT_BEGIN, OBJECT_CHUNK, OBJECT_FINISH, OBJECT_CANCEL = range(11, 16)
OUTPUT, RESULT, PIXELS, DATA = 128, 129, 130, 131


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
    """The host port runs the shared handler and interpreter with native pipes."""
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


class SerialTransport:
    def __init__(self, port, no_reset=False):
        import serial
        if no_reset:
            # Set control lines before opening: ESP32 USB Serial/JTAG interprets
            # DTR/RTS transitions as reset/download requests.
            self.serial = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=3)
            self.serial.dtr = False
            self.serial.rts = False
            self.serial.port = port
            try:
                self.serial.open()
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


class Client:
    def __init__(self, transport, timeout=5, output=None):
        self.transport = transport
        self.timeout = timeout
        self.output = output or (lambda data: None)
        self.request_id = 0
        self.pixel_snapshot = bytearray()
        self.data_reply = bytearray()

    def read_exact(self, size, deadline):
        result = bytearray()
        while len(result) < size:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProtocolError("timed out waiting for receiver")
            result.extend(self.transport.read(size - len(result), remaining))
        return bytes(result)

    def command(self, kind, payload=b""):
        self.data_reply.clear()
        if kind == FRAME:
            self.pixel_snapshot.clear()
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
            elif kind == PIXELS:
                self.pixel_snapshot.extend(payload)
            elif kind == DATA:
                self.data_reply.extend(payload)
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

    def status_fields(self):
        reply = self.require_ok(self.command(STATUS))
        # animation_error may contain spaces; capability/limit tokens do not.
        return dict(item.split("=", 1) for item in reply.message.split() if "=" in item)

    def upload(self, bytecode):
        fields = self.status_fields()
        try:
            limit = int(fields.get("module_limit", MODULE_LIMIT))
        except ValueError as error:
            raise ProtocolError("receiver reported an invalid module limit") from error
        if limit < 8:
            raise ProtocolError("receiver reported an invalid module limit")
        if not 8 <= len(bytecode) <= limit:
            raise ValueError(f"module must be 8..{limit} bytes for this receiver")
        self.require_ok(self.command(BEGIN, struct.pack("<II", len(bytecode), zlib.crc32(bytecode))))
        for offset in range(0, len(bytecode), FRAME_LIMIT - 4):
            chunk = bytecode[offset:offset + FRAME_LIMIT - 4]
            reply = self.require_ok(self.command(CHUNK, struct.pack("<I", offset) + chunk))
            if reply.value != offset + len(chunk):
                raise ProtocolError("receiver acknowledged an unexpected upload offset")
        return self.require_ok(self.command(FINISH))


    def manage(self, operation, **fields):
        payload = json.dumps({"op": operation, **fields}, separators=(",", ":")).encode()
        return self.require_ok(self.command(MANAGE, payload))

    def describe(self):
        self.manage("describe")
        return json.loads(self.data_reply)

    def get_config(self, saved=False):
        self.manage("config_saved" if saved else "config_get")
        return json.loads(self.data_reply)

    def upload_object(self, kind, data, name=None):
        capabilities = self.describe()
        limit = capabilities["config_limit" if kind == "config" else "module_limit"]
        if not (1 if kind == "config" else 8) <= len(data) <= limit:
            raise ValueError(f"{kind} exceeds receiver size limit {limit}")
        metadata = {"kind": kind, "size": len(data), "crc32": zlib.crc32(data)}
        if name is not None:
            metadata["name"] = name
        self.require_ok(self.command(OBJECT_BEGIN, json.dumps(metadata).encode()))
        for offset in range(0, len(data), FRAME_LIMIT - 4):
            chunk = data[offset:offset + FRAME_LIMIT - 4]
            reply = self.require_ok(self.command(OBJECT_CHUNK, struct.pack("<I", offset) + chunk))
            if reply.value != offset + len(chunk):
                raise ProtocolError("receiver acknowledged an unexpected object offset")
        return self.require_ok(self.command(OBJECT_FINISH))

    def configure(self, config, save=False):
        revision = self.describe()["active_revision"]
        data = json.dumps(config, separators=(",", ":")).encode()
        self.upload_object("config", data)
        self.manage("config_validate")
        reply = self.manage("config_apply", expected_revision=revision)
        if save:
            self.save_config()
        return reply

    def save_config(self):
        description = self.describe()
        return self.manage("config_save", expected_revision=description["active_revision"],
                           expected_saved_revision=description["saved_revision"])

    def save_procedure(self, name, data):
        revision = self.describe()["catalog_revision"]
        self.upload_object("procedure", data, name)
        return self.manage("procedure_save", name=name, expected_revision=revision)

    def procedures(self):
        self.manage("procedure_list")
        return json.loads(self.data_reply)

    def get_procedure(self, name):
        self.manage("procedure_get", name=name)
        return bytes(self.data_reply)

    def delete_procedure(self, name):
        revision = self.describe()["catalog_revision"]
        return self.manage("procedure_delete", name=name, expected_revision=revision)

    def activate(self, name, action="play", export="run"):
        return self.manage("procedure_activate", name=name, action=action, export=export)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--port", help="USB serial port, e.g. /dev/cu.usbmodem...")
    transport.add_argument("--process", type=Path, help="desktop receiver executable")
    parser.add_argument("--no-reset", action="store_true", help="deassert DTR/RTS before opening an ESP32 USB serial port")
    parser.add_argument("--storage", type=Path, help="host persistent store file")
    parser.add_argument("--save", action="store_true", help="save an applied --config for reboot")
    parser.add_argument("--saved", action="store_true", help="read saved rather than active configuration")
    parser.add_argument("--action", choices=("play", "run"), default="play")
    parser.add_argument("--output", type=Path, help="binary readback destination")
    parser.add_argument("--function", default="run", help="zero-argument exported function")
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--describe", action="store_true")
    action.add_argument("--config", type=Path, help="apply JSON configuration in RAM")
    action.add_argument("--validate-config", type=Path, help="stage and validate JSON without applying")
    action.add_argument("--get-config", action="store_true")
    action.add_argument("--save-config", action="store_true")
    action.add_argument("--store-procedure", metavar="NAME", help="save the supplied module under this name")
    action.add_argument("--procedures", action="store_true")
    action.add_argument("--get-procedure", metavar="NAME")
    action.add_argument("--delete-procedure", metavar="NAME")
    action.add_argument("--activate", metavar="NAME")
    action.add_argument("--cancel-upload", action="store_true")
    action.add_argument("--status", action="store_true", help="query receiver without uploading")
    action.add_argument("--animate", action="store_true", help="start render(index) animation on the device")
    action.add_argument("--stop", action="store_true", help="stop animation and clear the LEDs")
    action.add_argument("--native-demo", action="store_true", help="start the built-in native rainbow")
    action.add_argument("--frame-ms", type=int, help="render one frame with this delta and report an RGB preview")
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("module", nargs="?", type=Path)
    args = parser.parse_args()
    if args.storage and not args.process:
        parser.error("--storage requires --process")
    if args.save and not args.config:
        parser.error("--save requires --config")
    if args.saved and not args.get_config:
        parser.error("--saved requires --get-config")
    if args.output and not args.get_procedure:
        parser.error("--output requires --get-procedure")
    if args.store_procedure and args.module is None:
        parser.error("--store-procedure requires a .wasm module")
    management = (args.describe or args.config or args.validate_config or args.get_config or args.save_config or
                  args.store_procedure or args.procedures or args.get_procedure or args.delete_procedure or
                  args.activate or args.cancel_upload)
    if management and args.module is not None and not args.store_procedure:
        parser.error("module argument only applies to --store-procedure or legacy execution")
    if args.no_reset and not args.port:
        parser.error("--no-reset requires --port")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.frame_ms is not None and not 0 <= args.frame_ms <= 1000:
        parser.error("--frame-ms must be 0..1000")
    if args.module is None and not (args.status or args.animate or args.stop or args.native_demo or args.frame_ms is not None or management):
        parser.error("provide a .wasm module or a status/animation command")
    connection = None
    try:
        connection = SerialTransport(args.port, no_reset=args.no_reset) if args.port else ProcessTransport(args.process, storage=args.storage)
        def console(data):
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
        client = Client(connection, timeout=args.timeout, output=console)
        if management:
            if args.describe:
                print(json.dumps(client.describe(), indent=2))
            elif args.get_config:
                print(json.dumps(client.get_config(saved=args.saved), indent=2))
            elif args.procedures:
                print(json.dumps(client.procedures(), indent=2))
            elif args.get_procedure:
                data = client.get_procedure(args.get_procedure)
                if args.output:
                    args.output.write_bytes(data)
                else:
                    sys.stdout.buffer.write(data)
            elif args.config:
                client.configure(json.loads(args.config.read_text()), save=args.save)
            elif args.validate_config:
                client.upload_object("config", args.validate_config.read_bytes())
                client.manage("config_validate")
            elif args.save_config:
                client.save_config()
            elif args.store_procedure:
                client.save_procedure(args.store_procedure, args.module.read_bytes())
            elif args.delete_procedure:
                client.delete_procedure(args.delete_procedure)
            elif args.activate:
                client.activate(args.activate, args.action, args.function)
            elif args.cancel_upload:
                client.require_ok(client.command(OBJECT_CANCEL))
            return 0
        if args.module is not None:
            loaded = client.upload(args.module.read_bytes())
            print(f"Loaded {loaded.value} bytes", file=sys.stderr)
        if args.animate:
            reply = client.command(PLAY)
        elif args.stop:
            reply = client.command(STOP)
        elif args.native_demo:
            reply = client.command(NATIVE_DEMO)
        elif args.frame_ms is not None:
            reply = client.command(FRAME, struct.pack("<I", args.frame_ms))
            if client.pixel_snapshot:
                colors = [tuple(client.pixel_snapshot[index:index+3])
                          for index in range(0, min(15, len(client.pixel_snapshot)), 3)]
                print(f"RGB snapshot: {len(client.pixel_snapshot)//3} pixels; first five {colors}", file=sys.stderr)
        elif args.status or args.module is None:
            reply = client.command(STATUS)
        else:
            reply = client.command(RUN, args.function.encode("utf-8"))
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
