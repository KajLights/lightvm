"""The saved Python example renders through the ordinary bytecode interpreter."""
import json
import colorsys
import os
from pathlib import Path
import struct
import tempfile
import unittest

from bytecode_fixtures import ROOT, sample
from test_configuration import config
from client import Client, ProcessTransport, FRAME, STOP


class AudioFeatureExampleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.client = Client(ProcessTransport(os.environ.get("LIGHTVM_RECEIVER", ROOT / "build/host/lightvm"),
                                              Path(self.temp.name) / "store.bin"))
        self.client.configure(config(200))
        self.bindings = json.loads((ROOT / "examples/compositions/audio-features.json").read_text())["layers"][0]["inputs"]
        self.publisher = self.client.claim_inputs({
            binding["source"]: "int" if name == "onsets" else "float"
            for name, binding in self.bindings.items()})
        self.sequence = 0
        self.client.upload(sample("audio_features"), inputs=self.bindings)

    def tearDown(self):
        self.client.close()
        self.temp.cleanup()

    def publish(self, **changes):
        values = {binding["source"]: 0 if name == "onsets" else 0.0 for name, binding in self.bindings.items()}
        values.update({"audio." + name: value for name, value in changes.items()})
        self.sequence += 1
        self.client.publish_inputs(self.publisher, self.sequence, values)

    def frame(self, ms=0):
        self.client.require_ok(self.client.command(FRAME, struct.pack("<I", ms)))
        return bytes(self.client.pixel_snapshot)

    def test_six_sections_and_compiled_example(self):
        self.publish(level=1.0, bass=0.5, mid=0.25, treble=0.75, centroid=0.5, onset=0.5)
        pixels = self.frame()
        self.assertEqual(len(pixels), 600)
        rgb = [tuple(pixels[i:i+3]) for i in range(0, len(pixels), 3)]
        self.assertGreater(rgb[10][1], 100)  # Green level.
        self.assertGreater(rgb[40][0], 100)  # Red bass.
        self.assertLess(max(rgb[60]), 10)    # Beyond half-height bass.
        self.assertGreater(rgb[70][0], 100)  # Amber mid.
        self.assertGreater(rgb[110][2], 100) # Blue treble.
        self.assertGreater(rgb[148][2], 100) # Purple centroid marker.
        self.assertGreater(rgb[175][0], 100) # Magenta onset envelope.
        self.assertFalse(self.client.program_state()["faulted"])

    def test_onset_counter_flashes_once_then_decays(self):
        self.publish(onsets=10)
        self.frame()  # Initial attachment does not flash for an old counter.
        self.publish(onsets=11)
        white = self.frame(1)[180*3:181*3]
        self.assertTrue(all(channel > 240 for channel in white))
        self.publish(onsets=11)  # A repeated occurrence must not restart the flash.
        quiet = self.frame(200)[180*3:181*3]
        self.assertLess(max(quiet), 10)
        self.publish(onsets=12)
        self.assertTrue(all(channel > 240 for channel in self.frame(1)[180*3:181*3]))

    def test_coiled_pulse_uniform_silence_and_music(self):
        bindings = json.loads((ROOT / "examples/compositions/audio-pulse.json").read_text())["layers"][0]["inputs"]
        self.client.upload(sample("audio_pulse"), inputs=bindings)
        self.publish(level=0.2, bass=0.2)
        self.assertEqual(max(self.frame()), 0)
        self.publish(level=0.6, bass=0.6)
        pixels = self.frame(1)
        self.assertEqual(pixels, pixels[:3] * 200)
        self.assertGreater(pixels[0], 100)
        self.assertGreater(pixels[0], pixels[1])
        self.assertEqual(pixels[2], 0)
        self.publish(level=0.6, bass=0.6, onsets=1)
        self.assertTrue(all(channel > 240 for channel in self.frame(1)))
        self.publish(level=0.6, bass=0.6, onsets=1)
        self.assertEqual(self.frame(200)[2], 0)

    def test_sixteen_spectrum_sections_and_shared_flash(self):
        layers = json.loads((ROOT / "examples/compositions/audio-spectrum.json").read_text())["layers"]
        groups = []
        for start in (0, 6, 12):
            names = [f"audio.band{i:02d}" for i in range(start, min(start + 6, 16))]
            groups.append((self.client.claim_inputs({name: "float" for name in names}), names))
        self.client.save_program("audio_spectrum", sample("audio_spectrum"))
        self.client.configure_composition(layers)
        self.client.require_ok(self.client.command(STOP))
        for layer in layers:
            self.client.reset_program(layer=layer["id"])
        self.publish(level=0.42, onsets=0)
        for group, names in groups:
            self.client.publish_inputs(group, 1, {name: 0.1 + 0.4 * (int(name[-2:]) + 1) / 17 for name in names})
        pixels = self.frame()
        for i in range(200):
            band = i * 16 // 200
            expected = colorsys.hsv_to_rgb(band * 0.78 / 15, 1.0, (band + 1) / 17)
            for actual, value in zip(pixels[3*i:3*i+3], expected):
                self.assertAlmostEqual(actual, round(value * 255), delta=1)
        self.publish(level=0.42, onsets=1)
        self.assertTrue(all(channel > 240 for channel in self.frame(1)))
        self.publish(level=0.42, onsets=1)
        self.assertEqual(self.frame(200), pixels)  # Duplicate count does not flash again.
        for group, names in groups:
            self.client.publish_inputs(group, 2, {name: 0.0 for name in names})
        self.assertEqual(max(self.frame(1)), 0)
        for layer in layers:
            self.assertFalse(self.client.program_state(layer=layer["id"])["faulted"])

    def test_spectrum_auto_level_preserves_contrast_and_recovers(self):
        bindings = {name: {"value": value} for name, value in
                    dict(band0=0.25, band1=0.35, band2=0.45, band3=0.55,
                         level=0.55, onsets=0).items()}
        self.client.upload(sample("audio_spectrum"), inputs=bindings)
        reference = self.frame()
        self.assertGreater(max(reference[39*3:40*3]), max(reference[:3]))
        # Raise all logarithmic levels equally. A shared reference should
        # converge to the same four brightnesses without flattening contrast.
        self.client.set_inputs({name: {"value": binding["value"] + 0.30}
                                for name, binding in bindings.items() if name != "onsets"})
        self.assertGreater(sum(self.frame(20)), sum(reference))
        for _ in range(100): raised = self.frame(20)
        for actual, expected in zip(raised, reference):
            self.assertAlmostEqual(actual, expected, delta=1)
        self.client.set_inputs(bindings)
        for _ in range(100): recovered = self.frame(200)
        for actual, expected in zip(recovered, reference):
            self.assertAlmostEqual(actual, expected, delta=1)
        # Long quiet gaps must not turn weak noise into a bright rainbow.
        self.client.set_inputs({name: {"value": 0.05} for name in bindings if name != "onsets"})
        for _ in range(100): quiet = self.frame(200)
        self.assertEqual(max(quiet), 0)
        self.client.set_inputs(bindings)
        restored = self.frame(20)
        for actual, expected in zip(restored, reference):
            self.assertAlmostEqual(actual, expected, delta=1)
        self.assertFalse(self.client.program_state()["faulted"])

    def test_spectrum_no_flash_preset_ignores_onsets(self):
        layers = json.loads((ROOT / "examples/compositions/audio-spectrum-no-flash.json").read_text())["layers"]
        groups = []
        for start in (0, 6, 12):
            names = [f"audio.band{i:02d}" for i in range(start, min(start + 6, 16))]
            groups.append((self.client.claim_inputs({name: "float" for name in names}), names))
        self.client.save_program("audio_spectrum", sample("audio_spectrum"))
        self.client.configure_composition(layers)
        self.client.require_ok(self.client.command(STOP))
        for layer in layers:
            self.client.reset_program(layer=layer["id"])
        self.publish(level=0.42, onsets=0)
        for publisher, names in groups:
            self.client.publish_inputs(publisher, 1, {name: 0.35 for name in names})
        reference = self.frame()
        self.assertGreater(max(reference), 100)
        for count in (1, 2, 20, 200):
            self.publish(level=0.42, onsets=count)
            self.assertEqual(self.frame(1), reference)
            self.assertEqual(self.frame(100), reference)
        # Spectrum energy still controls brightness with flashes disabled.
        for publisher, names in groups:
            self.client.publish_inputs(publisher, 2, {name: 0.0 for name in names})
        self.assertEqual(max(self.frame(1)), 0)
        for layer in layers:
            self.assertFalse(self.client.program_state(layer=layer["id"])["faulted"])


if __name__ == "__main__":
    unittest.main()
