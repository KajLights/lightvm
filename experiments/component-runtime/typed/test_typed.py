"""Behavioral and artifact-adversarial checks against the compiled C runtime."""
import json
import os
import random
import struct
import subprocess
import unittest

from assembler import OP, effect_package, replacement_package
from run import BUILD, build


class TypedVMTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary, cls.package, _ = build(sanitize=os.environ.get("TYPED_SANITIZE") == "1")
        cls.fixture = BUILD / (cls.binary.name + "-malformed.tvm")

    def validates(self, raw):
        self.fixture.write_bytes(raw)
        result = subprocess.run([str(self.binary), "--validate", str(self.fixture)], capture_output=True)
        self.assertIn(result.returncode, (0, 2), "loader crashed")
        return result.returncode == 0

    def test_integrated_runtime_and_ownership(self):
        result = subprocess.run([str(self.binary), str(self.package)], check=True, capture_output=True, text=True)
        data = json.loads(result.stdout)
        self.assertTrue(all(data["checks"].values()), data["checks"])
        self.assertEqual(data["metrics"]["tracked_component_final_bytes"], 0)
        self.assertEqual(data["metrics"]["pixel_data_copies"], 0)
        self.assertGreater(data["metrics"]["workload_resume_calls"], 6)

    def test_valid_and_truncated_artifacts(self):
        raw = effect_package()
        self.assertTrue(self.validates(raw))
        self.assertTrue(self.validates(replacement_package()))
        for length in (0, 1, 4, 31, 32, len(raw)-1):
            with self.subTest(length=length):
                self.assertFalse(self.validates(raw[:length]))
        self.assertFalse(self.validates(raw + b"\x00"))

    def test_header_limits(self):
        for offset, value in ((4, 2), (8, 0), (8, 0xFFFFFFFF), (12, 99999),
                              (16, 99999), (20, 9), (24, 0), (24, 9), (28, 0), (28, 2)):
            raw = bytearray(effect_package())
            struct.pack_into("<I", raw, offset, value)
            with self.subTest(offset=offset, value=value):
                self.assertFalse(self.validates(raw))

    def test_instruction_classes_and_targets(self):
        # Replace the first instruction so each rejection exercises the real verifier.
        invalid = [(255,0,0,0,0), (OP["const"],16,0,0,0),
                   (OP["add"],0,0,16,0), (OP["shl"],0,0,0,32),
                   (OP["jmp"],0,0,0,99999), (OP["call"],0,0,0,99999),
                   (OP["jnz"],16,0,0,0), (OP["store8"],0,0,2,0),
                   (OP["state_load"],0,0,0,8), (OP["ret"],1,0,0,0)]
        for instruction in invalid:
            raw = bytearray(effect_package())
            raw[32:40] = struct.pack("<BBBBI", *instruction)
            with self.subTest(instruction=instruction):
                self.assertFalse(self.validates(raw))

    def test_random_malformed_streams_do_not_crash_loader(self):
        rng = random.Random(0xC0DEC)
        for _ in range(64):
            raw = bytearray(effect_package())
            instruction = rng.randrange((len(raw)-32)//8)
            raw[32+instruction*8] = rng.randrange(19,256)
            self.assertFalse(self.validates(raw))


if __name__ == "__main__":
    unittest.main()
