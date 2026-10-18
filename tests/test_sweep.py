"""Shared sweep motion, retained paint, sampled trails and memory admission."""
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from client import Client, ProcessTransport, FRAME, STOP, ProtocolError
from lightvm.compiler import compile_source

SOURCE = (ROOT / 'examples/animations/sweep.py').read_text()


class SweepTests(unittest.TestCase):
    def setUp(self):
        self.client = Client(ProcessTransport(os.environ.get('LIGHTVM_RECEIVER', str(ROOT / 'build/unified-animation-host/lightvm'))))
        self.configure(5)
        program = compile_source(SOURCE)
        self.assertTrue(program.success, program.diagnostics)
        self.client.save_program('sweep', program.bytecode)

    def tearDown(self):
        self.client.close()

    def configure(self, count):
        self.count = count
        self.client.configure({'schema': 'lightvm.config', 'outputs': [
            {'id': 'lights', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': count}}]})

    def load(self, **settings):
        self.client.configure_composition([{'id': 'eyes', 'program': 'sweep', 'settings': {
            'eye_count': 1, 'eye_width': 1, 'rainbow_eyes': False, 'eased': False,
            **settings}}], output='lights')
        self.client.require_ok(self.client.command(STOP, output='lights'))

    def frame(self, ms=0):
        self.client.require_ok(self.client.command(FRAME, struct.pack('<I', ms), output='lights'))
        return bytes(self.client.snapshot(output='lights', layer='eyes')[1])

    def alphas(self, ms=0):
        return list(self.frame(ms)[3::4])

    def test_clean_bounce_loop_reverse_and_single_pixel(self):
        self.load(motion=1)
        self.assertEqual(self.alphas(), [255, 0, 0, 0, 0])
        self.assertEqual(self.alphas(1000), [0, 0, 0, 0, 255])
        self.assertEqual(self.alphas(1000), [255, 0, 0, 0, 0])
        self.load(motion=0, reverse=True)
        self.assertEqual(self.alphas(), [0, 0, 0, 0, 255])
        self.assertEqual(self.alphas(1000), [0, 0, 0, 0, 255])
        self.configure(1)
        self.load(eye_width=1)
        self.assertEqual(self.alphas(1000), [255])

    def test_retained_forward_chase_and_inward_wipe(self):
        self.load(motion=2, retain=True)
        self.assertEqual(self.alphas(500), [255, 255, 255, 0, 0])
        self.assertEqual(self.alphas(500), [255] * 5)
        self.assertEqual(self.alphas(1000), [255] * 5)
        self.load(motion=3, eye_count=2, retain=True)
        self.assertEqual(self.alphas(), [255, 0, 0, 0, 255])
        self.assertEqual(self.alphas(750), [255] * 5)
        self.assertEqual(self.alphas(250), [255, 0, 0, 0, 255])
        self.load(motion=1, eye_count=2, retain=True)
        self.assertEqual(self.alphas(), [255, 0, 0, 0, 255])
        self.assertEqual(self.alphas(500), [255] * 5)
        self.load(motion=3, eye_count=2, retain=True, reverse=True)
        self.assertEqual(self.alphas(), [0, 0, 255, 0, 0])
        self.assertEqual(self.alphas(500), [0, 255, 255, 255, 0])

    def test_trail_opacity_age_startup_and_expiry(self):
        self.load(motion=2, trail_seconds=1.0, samples=5)
        self.assertEqual(self.alphas(), [255, 0, 0, 0, 0])
        self.assertEqual(self.alphas(750), [64, 128, 191, 255, 0])
        self.assertEqual(self.alphas(250), [0, 64, 128, 191, 255])
        self.assertEqual(self.alphas(1000), [0, 0, 0, 0, 255])

    def test_matching_times_ignore_frame_schedule_and_repeated_frames(self):
        for settings in ({'motion': 1, 'trail_seconds': 0.8, 'samples': 16, 'eye_count': 2, 'eased': True},
                         {'motion': 0, 'trail_seconds': 0.5, 'samples': 12},
                         {'motion': 3, 'retain': True, 'eye_count': 2}):
            frames = []
            for schedule in ([1000, 234], [10] * 123 + [4], [407, 827]):
                self.load(**settings)
                for ms in schedule:
                    result = self.frame(ms)
                self.assertEqual(result, self.frame())
                frames.append(result)
            self.assertEqual(frames[0], frames[1])
            self.assertEqual(frames[1], frames[2])

    def test_batched_eyes_share_frames_and_rendering_does_not_allocate(self):
        self.configure(100)
        self.load(eye_count=1)
        single = self.client.program_state(output='lights', layer='eyes')
        self.load(eye_count=4)
        batch = self.client.program_state(output='lights', layer='eyes')
        self.assertEqual(batch['buffer_bytes'], single['buffer_bytes'])
        self.assertEqual(batch['state_bytes'] - single['state_bytes'], 3 * 104)
        heap = self.client.status_fields()['heap']
        for _ in range(20):
            self.frame(37)
        self.assertEqual(self.client.status_fields()['heap'], heap)
        before = self.frame()
        with self.assertRaises(ProtocolError):
            self.load(eye_count=32, samples=128, trail_seconds=1.0)
        self.assertEqual(self.client.status_fields()['heap'], heap)
        self.assertEqual(self.frame(), before)


if __name__ == '__main__':
    unittest.main()
