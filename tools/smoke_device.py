#!/usr/bin/env python3
"""Check a LightVM port using real WASM; replaces resident programs and stops playback."""
import argparse
from pathlib import Path
import struct
import sys
import time
import zlib

import wasmtime

from client import (BEGIN, CHUNK, FINISH, FRAME, NATIVE_DEMO, PLAY, RESET, RUN,
                    STATUS, STOP, Client, ProcessTransport, ProtocolError,
                    SerialTransport, frame)

ROOT = Path(__file__).resolve().parents[1]
PROFILES = {
    "simulator": ("host", "memory", -1, 255),
    "test_175": ("host", "memory", -1, 255),
    "pico": ("pico", "ws2812", 16, 32),
    "xiao_esp32s3": ("esp32", "ws2812", 1, 32),
    "xiao_esp32c6": ("esp32", "ws2812", 0, 32),
}


def check(condition, message):
    if not condition:
        raise ProtocolError(message)


def wasm(text):
    return bytes(wasmtime.wat2wasm(text))


def command(client, kind, payload=b"", code=0, value=None):
    reply = client.command(kind, payload)
    check(reply.code == code, f"command {kind}: expected status {code}, got {reply}")
    if value is not None:
        check(reply.value == value, f"command {kind}: expected value {value}, got {reply.value}")
    return reply


def uploaded(client, text):
    client.upload(wasm(text))


def smoke(client, captured, board, reference):
    fields = client.status_fields()
    check(fields["board"] == board, f"expected board {board}, got {fields['board']}")
    check(fields["api"] == "1" and fields["protocol"] == "1", "incompatible API/protocol")
    if board in PROFILES:
        platform, output, pin, brightness = PROFILES[board]
        check((fields["platform"], fields["output"], int(fields["pin"]), int(fields["brightness"])) ==
              (platform, output, pin, brightness), "incorrect board metadata")
    print(f"Identity: {fields['platform']}/{board}, {fields['pixels']} pixels; initial mode={fields['mode']}")
    command(client, RESET)
    captured.clear()
    hello = (ROOT / "samples/hello.wat").read_text()
    uploaded(client, hello)
    command(client, RUN, value=42)
    check(captured == b"Hello from dynamically uploaded WASM!\n", "console bytes differ")
    captured.clear()
    uploaded(client, (ROOT / "samples/wasi_stdout.wat").read_text())
    command(client, RUN, value=0)
    check(captured == b"Hello through WASI stdout!\n", "WASI bytes differ")
    uploaded(client, (ROOT / "samples/counter.wat").read_text())
    command(client, RUN, value=1)
    command(client, RUN, value=2)
    command(client, BEGIN, struct.pack("<II", int(fields["module_limit"]) + 1, 0), code=2)
    command(client, RUN, value=3)  # A rejected BEGIN retains the resident module.
    uploaded(client, (ROOT / "samples/counter.wat").read_text())
    command(client, RUN, value=1)
    command(client, RESET)
    check(client.status_fields()["heap"] == "0", "RESET did not release interpreter memory")
    print("Uploads, console/WASI, retained state, replacement and upload limit: passed")

    # Ordered transfer, duplicate retry, incomplete FINISH and bad module CRC.
    data = wasm('(module (func (export "run") (result i32) i32.const 9))')
    command(client, BEGIN, struct.pack("<II", len(data), zlib.crc32(data)))
    first = struct.pack("<I", 0) + data[:8]
    command(client, CHUNK, first, value=8)
    command(client, CHUNK, first, value=8)
    command(client, FINISH, code=2)
    command(client, CHUNK, struct.pack("<I", 9) + data[8:], code=2)
    command(client, CHUNK, struct.pack("<I", 8) + data[8:], value=len(data))
    command(client, FINISH)
    command(client, RUN, value=9)
    command(client, BEGIN, struct.pack("<II", len(data), zlib.crc32(data) ^ 1))
    command(client, CHUNK, struct.pack("<I", 0) + data)
    command(client, FINISH, code=2)
    corrupt = bytearray(frame(STATUS, 65000))
    corrupt[12] ^= 1
    client.transport.write(corrupt)
    check(client.receive_reply(65000).code == 1, "corrupt frame was accepted")
    command(client, STATUS)

    rejected = [
        '(module (import "unknown" "service" (func $missing)) '
        '(func (export "run") call $missing))',
        '(module (memory 2) (func (export "run")))',
        '(module (memory 1) ' + '(func (result i32) i32.const 1)' * 600 + ')',
    ]
    for text in rejected:
        data = wasm(text)
        command(client, BEGIN, struct.pack("<II", len(data), zlib.crc32(data)))
        for offset in range(0, len(data), 508):
            command(client, CHUNK, struct.pack("<I", offset) + data[offset:offset+508])
        command(client, FINISH, code=3)
        check(client.status_fields()["heap"] == "0", "failed load leaked interpreter memory")
    limits = client.status_fields()
    check(int(limits["heap_peak"]) <= int(limits["heap_limit"]), "heap budget was exceeded")
    uploaded(client, '''(module
      (import "console" "write" (func $write (param i32 i32)))
      (memory 1) (func (export "run") i32.const 0 i32.const 16385 call $write))''')
    command(client, RUN, code=5)
    uploaded(client, '(module (func (export "run") (loop $again br $again)))')
    started = time.monotonic()
    command(client, RUN, code=5)
    check(time.monotonic() - started < 2.0, "cooperative timeout did not bound execution")
    command(client, STATUS)
    uploaded(client, hello)
    command(client, RUN, value=42)
    command(client, RESET)
    print("Malformed transfers, unsupported imports, memory limit and timeout recovery: passed")

    count = int(fields["pixels"])
    check(int(reference.status_fields()["pixels"]) == count,
          "reference pixel count differs; build/select a matching host profile")
    patterns = {
        "RGB": '''(module
          (import "led" "rgb" (func $rgb (param i32 i32 i32)))
          (func (export "render") (param $i i32)
            local.get $i i32.const 256 i32.rem_u
            i32.const 80 i32.const 255 call $rgb))''',
        "HSV": (ROOT / "samples/primary_colors.wat").read_text(),
        "rainbow": (ROOT / "samples/rainbow.wat").read_text(),
    }
    for name, text in patterns.items():
        data = wasm(text)
        command(client, RESET)
        command(reference, RESET)
        client.upload(data)
        reference.upload(data)
        for delta in (0, 16, 17, 1000):
            payload = struct.pack("<I", delta)
            command(client, FRAME, payload, value=count)
            command(reference, FRAME, payload, value=count)
            check(len(client.pixel_snapshot) == count * 3, f"{name}: wrong snapshot size")
            check(client.pixel_snapshot == reference.pixel_snapshot,
                  f"{name}: snapshot differs from host at delta {delta}")
    print("Identical WASM and stepped RGB/HSV/rainbow snapshots against host: passed")
    client.upload(wasm(patterns["rainbow"]))
    command(client, PLAY)
    time.sleep(0.15)
    running = client.status_fields()
    check(running["mode"] == "wasm" and int(running["frames"]) > 0, "idle playback failed")
    time.sleep(0.15)
    later = client.status_fields()
    check(int(later["frames"]) > int(running["frames"]), "frame clock stopped")
    check(later["heap"] == running["heap"] and later["heap_peak"] == running["heap_peak"],
          "playback heap did not remain stable")
    command(client, STOP)
    check(client.status_fields()["mode"] == "idle", "STOP did not stop playback")
    command(client, PLAY)
    check(client.status_fields()["mode"] == "wasm", "PLAY did not resume")
    command(client, NATIVE_DEMO)
    check(client.status_fields()["mode"] == "native", "native demo did not start")
    command(client, RESET)
    check(client.status_fields()["heap"] == "0", "final RESET leaked interpreter memory")
    print(f"Playback/STOP/resume/native demo and stable heap: passed (render={later['render_us']} us, output={later['show_us']} us)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--process", type=Path)
    transport.add_argument("--port")
    parser.add_argument("--board", required=True, help="expected STATUS board identity")
    parser.add_argument("--reference", type=Path, default=ROOT / "build/host/lightvm")
    parser.add_argument("--no-reset", action="store_true", help="deassert DTR/RTS before serial open")
    args = parser.parse_args()
    if args.no_reset and not args.port:
        parser.error("--no-reset requires --port")
    connection = reference_connection = None
    try:
        no_reset = args.no_reset or args.board.startswith("xiao_esp32")
        connection = SerialTransport(args.port, no_reset=no_reset) if args.port else ProcessTransport(args.process)
        reference_connection = ProcessTransport(args.reference)
        captured = bytearray()
        client = Client(connection, output=captured.extend)
        reference = Client(reference_connection)
        smoke(client, captured, args.board, reference)
        if args.port:
            # Retain a counter through a real serial close/open, then check
            # autonomous frames continue while the terminal is disconnected.
            uploaded(client, (ROOT / "samples/counter.wat").read_text())
            command(client, RUN, value=1)
            connection.close()
            connection = SerialTransport(args.port, no_reset=no_reset)
            client = Client(connection)
            command(client, RUN, value=2)
            uploaded(client, (ROOT / "samples/rainbow.wat").read_text())
            command(client, PLAY)
            before = client.status_fields()
            connection.close()
            time.sleep(0.5)
            connection = SerialTransport(args.port, no_reset=no_reset)
            client = Client(connection)
            after = client.status_fields()
            check(after["mode"] == "wasm" and int(after["frames"]) > int(before["frames"]),
                  "serial reconnect reset or stopped playback")
            check(after["heap"] == before["heap"], "serial reconnect changed heap usage")
            command(client, RESET)
            print("Serial close/reopen retains module state and autonomous playback: passed")
        print("Portable smoke checks passed. Physical colour order and wiring require observation.")
        return 0
    except (OSError, ValueError, ImportError, ProtocolError) as error:
        print(f"Smoke check failed: {error}", file=sys.stderr)
        return 1
    finally:
        if connection:
            connection.close()
        if reference_connection:
            reference_connection.close()


if __name__ == "__main__":
    raise SystemExit(main())
