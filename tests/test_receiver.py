"""Integration tests execute real wasm3 through the common wire protocol."""
import os
from pathlib import Path
import struct
import sys
import unittest
import zlib
import wasmtime

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from client import BEGIN, CHUNK, FINISH, RESET, RUN, STATUS, Client, ProcessTransport, frame


class ReceiverTests(unittest.TestCase):
    def setUp(self):
        self.transport = ProcessTransport(Path(os.environ.get("LIGHTVM_RECEIVER", ROOT / "build" / "host" / "lightvm")))
        self.output = bytearray()
        self.client = Client(self.transport, output=self.output.extend)

    def tearDown(self):
        self.transport.close()

    def upload_wat(self, text):
        return self.client.upload(bytes(wasmtime.wat2wasm(text)))

    def test_console_and_integer_result(self):
        self.upload_wat((ROOT / "samples" / "hello.wat").read_text())
        result = self.client.command(RUN)
        self.assertEqual((result.code, result.value), (0, 42))
        self.assertEqual(self.output, b"Hello from dynamically uploaded WASM!\n")

    def test_wasi_stdout(self):
        self.upload_wat((ROOT / "samples" / "wasi_stdout.wat").read_text())
        result = self.client.command(RUN)
        self.assertEqual((result.code, result.value), (0, 0))
        self.assertEqual(self.output, b"Hello through WASI stdout!\n")

    def test_start_output_and_initialization_run_once(self):
        self.upload_wat('''(module
          (import "console" "write" (func $write (param i32 i32)))
          (memory 1) (data (i32.const 0) "initializing\\0a")
          (global $value (mut i32) (i32.const 0))
          (func $init
            i32.const 0 i32.const 13 call $write
            i32.const 17 global.set $value)
          (start $init)
          (func (export "run") (result i32) global.get $value))''')
        result = self.client.command(RUN)
        self.assertEqual((result.code, result.value), (0, 17))
        self.assertEqual(self.output, b"initializing\n")
        self.assertEqual(self.client.command(RUN).value, 17)
        self.assertEqual(self.output, b"initializing\n")

    def test_start_function_loop_obeys_deadline(self):
        self.upload_wat('''(module
          (func $init (loop $again br $again))
          (start $init) (func (export "run")))''')
        result = self.client.command(RUN)
        self.assertEqual(result.code, 5)
        self.assertIn("deadline", result.message)
        self.assertEqual(self.client.command(STATUS).code, 0)

    def test_start_function_exit_returns_status(self):
        self.upload_wat('''(module
          (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
          (func $init i32.const 3 call $exit)
          (start $init) (func (export "run")))''')
        result = self.client.command(RUN)
        self.assertEqual((result.code, result.value), (6, 3))

    def test_float_math_and_integer_result(self):
        self.upload_wat('''(module (func (export "run") (result i32)
          f64.const 81 f64.sqrt f64.const 4 f64.mul i32.trunc_f64_s
          f32.const 9 f32.sqrt f32.const 2 f32.mul i32.trunc_f32_s
          i32.add))''')
        result = self.client.command(RUN)
        self.assertEqual((result.code, result.value), (0, 42))

    def test_replacement_and_state_without_restart(self):
        text = (ROOT / "samples" / "counter.wat").read_text()
        self.upload_wat(text)
        self.assertEqual(self.client.command(RUN).value, 1)
        self.assertEqual(self.client.command(RUN).value, 2)
        self.upload_wat(text)
        self.assertEqual(self.client.command(RUN).value, 1)
        self.client.command(RESET)
        self.assertIn("heap=0 ", self.client.command(STATUS).message)

    def test_chunk_retry_order_and_incomplete_upload(self):
        data = bytes(wasmtime.wat2wasm('(module (func (export "run") (result i32) i32.const 9))'))
        self.client.command(BEGIN, struct.pack("<II", len(data), zlib.crc32(data)))
        chunk = struct.pack("<I", 0) + data[:8]
        self.assertEqual(self.client.command(CHUNK, chunk).value, 8)
        self.assertEqual(self.client.command(CHUNK, chunk).value, 8)
        self.assertEqual(self.client.command(FINISH).code, 2)
        self.assertEqual(self.client.command(CHUNK, struct.pack("<I", 9) + data[8:]).code, 2)
        self.client.command(CHUNK, struct.pack("<I", 8) + data[8:])
        self.assertEqual(self.client.command(FINISH).code, 0)
        self.assertEqual(self.client.command(RUN).value, 9)

    def test_corrupt_frame_and_resynchronisation(self):
        packet = bytearray(frame(STATUS, 91))
        packet[-1] ^= 1
        self.transport.write(b"noise" + packet)
        self.assertEqual(self.client.receive_reply(91).code, 1)
        self.assertEqual(self.client.command(STATUS).code, 0)

    def test_bad_module_checksum(self):
        self.client.command(BEGIN, struct.pack("<II", 8, 0))
        self.client.command(CHUNK, struct.pack("<I", 0) + b"\0asm\1\0\0\0")
        self.assertEqual(self.client.command(FINISH).code, 2)
        self.assertEqual(self.client.command(RUN).code, 4)

    def test_bad_bytecode_releases_allocations(self):
        self.client.command(BEGIN, struct.pack("<II", 8, zlib.crc32(b"badbytes")))
        self.client.command(CHUNK, struct.pack("<I", 0) + b"badbytes")
        self.assertEqual(self.client.command(FINISH).code, 3)
        self.client.command(RESET)
        self.assertIn("heap=0 ", self.client.command(STATUS).message)

    def test_module_size_limit(self):
        limit = int(self.client.status_fields()["module_limit"])
        self.assertEqual(self.client.command(BEGIN, struct.pack("<II", limit + 1, 0)).code, 2)

    def test_platform_metadata_and_client_upload_limit(self):
        fields = self.client.status_fields()
        self.assertEqual(fields["api"], "1")
        self.assertEqual(fields["protocol"], "1")
        self.assertEqual(fields["platform"], "host")
        self.assertEqual(fields["output"], "memory")
        self.assertEqual(fields["pin"], "-1")
        self.assertEqual(fields["brightness"], "255")
        self.assertTrue(fields["board"])
        self.assertGreater(int(fields["frame_interval_us"]), 0)
        self.upload_wat('(module (func (export "run") (result i32) i32.const 23))')
        limit = int(fields["module_limit"])
        with self.assertRaisesRegex(ValueError, str(limit)):
            self.client.upload(bytes(limit + 1))
        self.assertEqual(self.client.command(RUN).value, 23)

    def test_linear_memory_limit(self):
        data = bytes(wasmtime.wat2wasm('(module (memory 2) (func (export "run")))'))
        self.client.command(BEGIN, struct.pack("<II", len(data), zlib.crc32(data)))
        self.client.command(CHUNK, struct.pack("<I", 0) + data)
        self.assertEqual(self.client.command(FINISH).code, 3)
        self.assertIn("heap=0 ", self.client.command(STATUS).message)

    def test_native_write_checks_memory_bounds(self):
        self.upload_wat('''(module
          (import "console" "write" (func $write (param i32 i32)))
          (memory 1) (func (export "run")
            i32.const 65535 i32.const 2 call $write))''')
        self.assertEqual(self.client.command(RUN).code, 4)
        self.assertEqual(self.output, b"")

    def test_runaway_loop_returns_status_and_receiver_recovers(self):
        self.upload_wat('(module (func (export "run") (loop $again br $again)))')
        result = self.client.command(RUN)
        self.assertEqual(result.code, 5)
        self.assertIn("deadline", result.message)
        self.assertEqual(self.client.command(STATUS).code, 0)

    def test_wasi_exit_status(self):
        self.upload_wat('''(module
          (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
          (func (export "_start") i32.const 3 call $exit))''')
        result = self.client.command(RUN, b"_start")
        self.assertEqual((result.code, result.value), (6, 3))

    def test_output_limit(self):
        self.upload_wat('''(module
          (import "console" "write" (func $write (param i32 i32)))
          (memory 1) (func (export "run")
            i32.const 0 i32.const 16385 call $write))''')
        self.assertEqual(self.client.command(RUN).code, 5)
        self.assertEqual(self.output, b"")

    def test_export_signature_and_missing_export(self):
        self.upload_wat('(module (func (export "run") (param i32)))')
        self.assertEqual(self.client.command(RUN).code, 4)
        self.assertEqual(self.client.command(RUN, b"absent").code, 4)

    def test_binary_console_and_multiple_output_frames(self):
        data = bytes(range(256)) * 3
        escaped = "".join(f"\\{value:02x}" for value in data)
        self.upload_wat(f'''(module
          (import "console" "write" (func $write (param i32 i32)))
          (memory 1) (data (i32.const 0) "{escaped}")
          (func (export "run") i32.const 0 i32.const {len(data)} call $write))''')
        self.assertEqual(self.client.command(RUN).code, 0)
        self.assertEqual(self.output, data)

    def test_unknown_import_is_rejected_and_another_module_can_load(self):
        data = bytes(wasmtime.wat2wasm('''(module
          (import "hardware" "missing" (func $missing))
          (func (export "run") call $missing))'''))
        self.client.command(BEGIN, struct.pack("<II", len(data), zlib.crc32(data)))
        self.client.command(CHUNK, struct.pack("<I", 0) + data)
        self.assertEqual(self.client.command(FINISH).code, 3)
        self.upload_wat('(module (func (export "run") (result i32) i32.const 23))')
        self.assertEqual(self.client.command(RUN).value, 23)

    def test_heap_limit_releases_failed_module(self):
        functions = "(func (result i32) i32.const 1)" * 600
        data = bytes(wasmtime.wat2wasm(f'(module (memory 1) {functions})'))
        self.client.command(BEGIN, struct.pack("<II", len(data), zlib.crc32(data)))
        for offset in range(0, len(data), 508):
            self.client.command(CHUNK, struct.pack("<I", offset) + data[offset:offset+508])
        self.assertEqual(self.client.command(FINISH).code, 3)
        status = self.client.command(STATUS)
        self.assertIn("heap=0 ", status.message)
        fields = dict(field.split("=", 1) for field in status.message.split())
        self.assertLessEqual(int(fields["heap_peak"]), int(fields["heap_limit"]))

    @unittest.skipUnless(os.name == "posix", "pseudo-terminal test requires POSIX")
    def test_serial_transport_uses_the_same_wire_protocol(self):
        import subprocess
        import tty
        from client import SerialTransport
        master, slave = os.openpty()
        tty.setraw(slave)
        receiver = subprocess.Popen(self.transport.process.args, stdin=master, stdout=master)
        os.close(master)
        connection = None
        try:
            connection = SerialTransport(os.ttyname(slave))
            output = bytearray()
            client = Client(connection, output=output.extend)
            client.upload(bytes(wasmtime.wat2wasm((ROOT / "samples" / "hello.wat").read_text())))
            reply = client.command(RUN)
            self.assertEqual((reply.code, reply.value), (0, 42))
            self.assertEqual(output, b"Hello from dynamically uploaded WASM!\n")
        finally:
            if connection:
                connection.close()
            os.close(slave)
            receiver.terminate()
            receiver.wait(timeout=2)


if __name__ == "__main__":
    unittest.main()
