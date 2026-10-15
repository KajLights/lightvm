"""Replay adapted effects against the original independent f64 Python samples."""
import os
import struct
import sys
import unittest
from bytecode_fixtures import ROOT, sample, program
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'tests/reference_effects'))
from replay import Replay
from client import Client, ProcessTransport, FRAME
from test_configuration import config

class PixelTests(unittest.TestCase):
    def setUp(self):
        self.transport=ProcessTransport(os.environ.get('LIGHTVM_RECEIVER',str(ROOT/'build/host/lightvm')))
        self.client=Client(self.transport)

    def tearDown(self):self.transport.close()

    def test_effects_match_original_at_varied_counts_and_deltas(self):
        for count in (1,5,175):
            self.client.configure(config(count))
            for name in ('color','pattern','rainbow','chase','wipe','twinkle'):
                with self.subTest(count=count,name=name):
                    self.client.upload(sample(name))
                    replay=Replay((ROOT/'tests/reference_effects'/(name+'.py')).read_text(),count)
                    for delta in (0,16,33,500,1000,0,1000):
                        self.client.require_ok(self.client.command(FRAME,struct.pack('<I',delta)))
                        actual=bytes(self.client.pixel_snapshot)
                        expected=replay.step(delta)
                        self.assertEqual(len(actual),count*3)
                        self.assertLessEqual(max(abs(a-b) for a,b in zip(actual,expected)),1 if name in ('rainbow','twinkle') else 0)

    def test_rgb_order_and_live_palette_controls(self):
        self.client.configure(config(7))
        self.client.upload(sample('pattern'))
        self.client.command(FRAME,bytes(4))
        self.assertEqual(bytes(self.client.pixel_snapshot),bytes.fromhex('ff000000ff000000ff')*2+bytes.fromhex('ff0000'))
        self.client.set_parameters({'fit':True})
        self.client.command(FRAME,bytes(4))
        self.assertEqual(bytes(self.client.pixel_snapshot),bytes.fromhex('ff0000')*3+bytes.fromhex('00ff00')*2+bytes.fromhex('0000ff')*2)

    def test_original_rainbow_matches_saved_wasm_frames(self):
        self.client.configure(config(5))
        self.client.upload(program((ROOT/'samples/rainbow.py').read_text()))
        # Captured from the original rainbow.wat module, using native HSV rules.
        # These include its 20 hue units/ms rate and both brightness waves.
        frames = (
            (0, '280000aad400007e3200327eaa00d4'),
            (16, '2d0100a9db00007733003183aa00cd'),
            (17, '330300a7e200007034002f8aab00c7'),
            (967, '0d4b0000aaaf2500a1590033f99d00'),
            (1000, '00267078008bc70c0026330000e268'),
            (1000, '94004b66470019ec0000383e3700bc'),
            (277, '280000aad400007e3200327eaa00d4'),
        )
        for delta, expected in frames:
            self.client.require_ok(self.client.command(FRAME,struct.pack('<I',delta)))
            self.assertEqual(bytes(self.client.pixel_snapshot),bytes.fromhex(expected))

    def test_rainbow_speed_is_independent_of_frame_intervals(self):
        self.client.configure(config(175))
        for code in (sample('rainbow'),program((ROOT/'samples/rainbow.py').read_text())):
            snapshots=[]
            # Each schedule advances exactly five seconds, at different rates
            # and with irregular frames. Compare motion, not frame counts.
            for schedule in ([1000]*5,[100]*50,[16,17,17]*100,[1,99,400,500]*5):
                self.client.upload(code)
                for delta in schedule:
                    self.client.require_ok(self.client.command(FRAME,struct.pack('<I',delta)))
                self.assertEqual(self.client.program_parameters()['program_us'],5000000)
                snapshots.append(bytes(self.client.pixel_snapshot))
            for actual in snapshots[1:]:
                self.assertLessEqual(max(abs(a-b) for a,b in zip(actual,snapshots[0])),1)
