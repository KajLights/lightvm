"""Real wasm3 rendering tests against the desktop native pixel buffer."""
import os
from pathlib import Path
import struct
import sys
import time
import unittest
import wasmtime

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from client import Client, ProcessTransport, FRAME, NATIVE_DEMO, PLAY, RESET, RUN, STATUS, STOP


class PixelTests(unittest.TestCase):
    def setUp(self):
        self.transport = ProcessTransport(Path(os.environ.get("LIGHTVM_RECEIVER", ROOT / "build/host/lightvm")))
        self.output = bytearray()
        self.client = Client(self.transport, output=self.output.extend)

    def tearDown(self):
        self.transport.close()

    def upload(self, text):
        return self.client.upload(bytes(wasmtime.wat2wasm(text)))

    def step(self, delta=0):
        return self.client.command(FRAME, struct.pack("<I", delta))

    def wait_for_status(self, expected, timeout=2):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            reply = self.client.command(STATUS)
            if expected in reply.message:
                return reply
            time.sleep(0.01)
        self.fail(f"did not reach {expected}: {reply.message}")

    def test_hsv_primary_colors_and_wrap(self):
        self.upload('''(module
          (import "led" "hsv" (func $hsv (param i32 i32 i32)))
          (func (export "render") (param $index i32)
            local.get $index i32.const 3 i32.rem_u
            i32.const 0 i32.eq
            if i32.const 65536 i32.const 255 i32.const 255 call $hsv
            else
              local.get $index i32.const 3 i32.rem_u i32.const 1 i32.eq
              if i32.const 21846 i32.const 255 i32.const 255 call $hsv
              else i32.const 43691 i32.const 255 i32.const 255 call $hsv end
            end))''')
        reply = self.step()
        self.assertEqual((reply.code, reply.value), (0, 100))
        self.assertEqual(self.client.pixel_snapshot[:9], bytes([255, 0, 0, 0, 255, 0, 0, 0, 255]))
        self.assertEqual(len(self.client.pixel_snapshot), 300)
        self.assertEqual(self.client.pixel_snapshot[-3:], bytes([255, 0, 0]))

    def test_rgb_per_pixel_and_native_count(self):
        self.upload('''(module
          (import "led" "pixel_count" (func $count (result i32)))
          (import "led" "rgb" (func $rgb (param i32 i32 i32)))
          (func (export "run") (result i32) call $count)
          (func (export "render") (param $index i32)
            local.get $index
            i32.const 255 local.get $index i32.sub
            i32.const 17 call $rgb))''')
        self.assertEqual(self.client.command(RUN).value, 100)
        self.assertEqual(self.step().code, 0)
        self.assertEqual(self.client.pixel_snapshot[:3], bytes([0, 255, 17]))
        self.assertEqual(self.client.pixel_snapshot[-3:], bytes([99, 156, 17]))

    def test_before_render_runs_once_and_retains_phase(self):
        self.upload('''(module
          (import "led" "hsv" (func $hsv (param i32 i32 i32)))
          (global $level (mut i32) (i32.const 0))
          (func (export "beforeRender") (param $delta i32)
            global.get $level local.get $delta i32.add global.set $level)
          (func (export "render") (param i32)
            i32.const 0 i32.const 0 global.get $level call $hsv))''')
        self.assertEqual(self.step(16).code, 0)
        self.assertEqual(self.client.pixel_snapshot, bytes([16, 16, 16]) * 100)
        self.assertEqual(self.step(4).code, 0)
        self.assertEqual(self.client.pixel_snapshot, bytes([20, 20, 20]) * 100)

    def test_rainbow_sample_scrolls_without_linear_memory(self):
        self.upload((ROOT / "samples/rainbow.wat").read_text())
        self.assertEqual(self.step().code, 0)
        first = bytes(self.client.pixel_snapshot)
        self.assertGreater(len(set(tuple(first[i:i+3]) for i in range(0, 300, 3))), 50)
        self.assertEqual(self.step(33).code, 0)
        self.assertNotEqual(first, self.client.pixel_snapshot)
        fields = dict(item.split("=", 1) for item in self.client.command(STATUS).message.split())
        self.assertLess(int(fields["heap"]), 65536)

    def test_color_range_and_render_context_are_checked(self):
        self.upload('''(module
          (import "led" "rgb" (func $rgb (param i32 i32 i32)))
          (func (export "run") i32.const 1 i32.const 2 i32.const 3 call $rgb)
          (func (export "render") (param i32)
            i32.const -1 i32.const 0 i32.const 0 call $rgb))''')
        self.assertEqual(self.client.command(RUN).code, 4)
        self.assertEqual(self.step().code, 4)
        self.assertEqual(self.client.pixel_snapshot, b"")
        self.assertEqual(self.client.command(STATUS).code, 0)

    def test_unwritten_pixels_are_black_each_frame(self):
        self.upload('''(module
          (import "led" "rgb" (func $rgb (param i32 i32 i32)))
          (global $phase (mut i32) (i32.const 0))
          (func (export "beforeRender") (param $delta i32)
            local.get $delta global.set $phase)
          (func (export "render") (param $index i32)
            global.get $phase i32.eqz
            local.get $index i32.eqz i32.or
            if i32.const 255 i32.const 0 i32.const 0 call $rgb end))''')
        self.assertEqual(self.step(0).code, 0)
        self.assertEqual(self.client.pixel_snapshot, bytes([255, 0, 0]) * 100)
        self.assertEqual(self.step(1).code, 0)
        self.assertEqual(self.client.pixel_snapshot, bytes([255, 0, 0]) + bytes(297))

    def test_render_signature_and_missing_export(self):
        for text in ['(module (func (export "run")))',
                     '(module (func (export "render") (param f32)))',
                     '(module (func (export "render") (param i32) (result i32) i32.const 0))']:
            with self.subTest(text=text):
                self.upload(text)
                self.assertEqual(self.client.command(PLAY).code, 4)
                self.assertEqual(self.step().code, 4)

    def test_before_render_signature_is_checked_repeatedly(self):
        self.upload('''(module
          (func (export "beforeRender") (param f64))
          (func (export "render") (param i32)))''')
        self.assertEqual(self.client.command(PLAY).code, 4)
        self.assertEqual(self.step().code, 4)

    def test_frame_console_and_rgb_snapshot_are_distinct(self):
        self.upload('''(module
          (import "console" "write" (func $write (param i32 i32)))
          (import "led" "rgb" (func $rgb (param i32 i32 i32)))
          (memory 1) (data (i32.const 0) "frame\\0a")
          (func (export "beforeRender") (param i32)
            i32.const 0 i32.const 6 call $write)
          (func (export "render") (param i32)
            i32.const 255 i32.const 0 i32.const 0 call $rgb))''')
        self.assertEqual(self.step().code, 0)
        self.assertEqual(self.output, b"frame\n")
        self.assertEqual(self.client.pixel_snapshot, bytes([255, 0, 0]) * 100)

    def test_play_renders_during_idle_input_and_stop_clears_mode(self):
        self.upload((ROOT / "samples/rainbow.wat").read_text())
        self.assertEqual(self.client.command(PLAY).code, 0)
        time.sleep(0.06)
        reply = self.client.command(STATUS)
        self.assertIn("mode=wasm", reply.message)
        fields = dict(item.split("=", 1) for item in reply.message.split())
        self.assertGreaterEqual(int(fields["frames"]), 2)
        self.assertEqual(self.client.command(STOP).code, 0)
        self.assertIn("mode=idle", self.client.command(STATUS).message)
        self.assertEqual(self.client.command(RESET).code, 0)
        self.assertIn("heap=0 ", self.client.command(STATUS).message)

    def test_background_runaway_stops_and_receiver_remains_usable(self):
        self.upload('(module (func (export "render") (param i32) (loop $again br $again)))')
        self.assertEqual(self.client.command(PLAY).code, 0)
        reply = self.wait_for_status("mode=idle")
        self.assertIn("execution deadline exceeded", reply.message)
        self.assertEqual(self.client.command(RESET).code, 0)

    def test_background_console_does_not_corrupt_serial_replies(self):
        self.upload('''(module
          (import "console" "write" (func $write (param i32 i32)))
          (memory 1) (data (i32.const 0) "log")
          (func (export "render") (param i32)
            i32.const 0 i32.const 3 call $write))''')
        self.assertEqual(self.client.command(PLAY).code, 0)
        reply = self.wait_for_status("mode=idle")
        self.assertIn("use FRAME", reply.message)
        self.assertEqual(self.output, b"")
        self.assertEqual(self.step().code, 0)
        self.assertEqual(self.output, b"log" * 100)

    def test_native_demo_works_without_a_module(self):
        self.assertEqual(self.client.command(NATIVE_DEMO).code, 0)
        self.assertIn("mode=native", self.client.command(STATUS).message)
        self.assertEqual(self.step().code, 0)
        self.assertEqual(len(self.client.pixel_snapshot), 300)
        self.assertEqual(self.client.command(STOP).code, 0)

    def test_control_payloads_and_frame_delta_bounds(self):
        for kind in (PLAY, STOP, NATIVE_DEMO):
            self.assertEqual(self.client.command(kind, b"bad").code, 1)
        self.assertEqual(self.client.command(FRAME).code, 1)
        self.assertEqual(self.step(1001).code, 1)


if __name__ == "__main__":
    unittest.main()
