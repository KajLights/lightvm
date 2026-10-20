"""Pico target selection, linked resource accounting and packaged UF2 safety."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_pico_ram import ram_report
from package_web_firmware import package, validate_pico_uf2


def uf2(count=2, family=0xE48BFF59, workaround=True):
    data = bytearray()
    if workaround:
        block = bytearray(512)
        struct.pack_into("<8I", block, 0, 0x0A324655, 0x9E5D5157, 0xA000,
                         0x10FFFF00, 256, 0, 2, 0xE48BFF57)
        block[32:288] = b"\xef" * 256
        struct.pack_into("<I", block, 288, 0x9957E304)
        struct.pack_into("<I", block, 508, 0x0AB16F30)
        data.extend(block)
    for n in range(count):
        block = bytearray(512)
        struct.pack_into("<8I", block, 0, 0x0A324655, 0x9E5D5157, 0x2000,
                         0x10000000 + n * 256, 256, n, count, family)
        struct.pack_into("<I", block, 508, 0x0AB16F30)
        data.extend(block)
    return data


class PicoUf2Tests(unittest.TestCase):
    def test_chip_families_are_not_interchangeable(self):
        for board in ("pico", "pico_w"):
            validate_pico_uf2(uf2(family=0xE48BFF56, workaround=False), 0x1017E000, board)
            with self.assertRaisesRegex(ValueError, "family"):
                validate_pico_uf2(uf2(workaround=False), 0x1017E000, board)
        validate_pico_uf2(uf2(), 0x1037E000, "pico2_w")
        with self.assertRaisesRegex(ValueError, "family"):
            validate_pico_uf2(uf2(family=0xE48BFF56), 0x1037E000, "pico2_w")

    def test_workaround_is_strictly_validated(self):
        for offset in (8, 12, 16, 20, 24, 32, 288, 292, 508):
            candidate = uf2()
            candidate[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                validate_pico_uf2(candidate, 0x1037E000, "pico2_w")
        with self.assertRaisesRegex(ValueError, "no application"):
            validate_pico_uf2(uf2()[:512], 0x1037E000, "pico2_w")

    def test_reserved_storage_and_malformed_blocks_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "reserved"):
            validate_pico_uf2(uf2(count=17), 0x10001000, "pico2_w")
        for offset in (0, 8, 12, 16, 20, 24, 28, 508):
            candidate = uf2()
            candidate[512 + offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                validate_pico_uf2(candidate, 0x1037E000, "pico2_w")
        for candidate in (b"", uf2()[:-1], uf2() + uf2()):
            with self.assertRaises(ValueError):
                validate_pico_uf2(candidate, 0x1037E000, "pico2_w")

    def test_package_records_chip_and_checks_layout_board(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "lightvm.uf2"
            image.write_bytes(uf2())
            layout = {"board": "pico2_w", "storage_start": 0x10380000,
                      "storage_bytes": 524288, "flash_bytes": 4194304, "firmware_end": 0x1037E000}
            layout_file = root / "storage-layout.json"
            layout_file.write_text(json.dumps(layout))
            package(root / "out", {}, {"pico2_w": image})
            manifest = json.loads((root / "out/pico.json").read_text())
            self.assertEqual(manifest["builds"][0]["chip"], "RP2350")
            self.assertEqual((root / "out/pico2_w/lightvm.uf2").read_bytes(), image.read_bytes())
            for key, value in (("board", "pico_w"), ("flash_bytes", 2097152),
                               ("firmware_end", 0x10381000), ("storage_bytes", True)):
                layout_file.write_text(json.dumps({**layout, key: value}))
                with self.subTest(key=key), self.assertRaisesRegex(ValueError, "layout"):
                    package(root / "out", {}, {"pico2_w": image})


class PicoRamTests(unittest.TestCase):
    def symbols(self, kib=520):
        top = 0x20000000 + kib * 1024
        return {"__lightvm_sram_start": 0x20000000, "__lightvm_sram_end": top,
                "__end__": 0x20020000, "__scratch_x_start__": top - 16384,
                "__scratch_x_end__": top - 16384, "__scratch_y_start__": top - 16384,
                "__scratch_y_end__": top - 16384, "__StackTop": top,
                "__StackBottom": top - 16384, "__HeapLimit": top - 16384, "__StackOneTop": top - 16384,
                "__StackOneBottom": top - 16384, "__lightvm_vm_heap": 65536,
                "__lightvm_pixel_capacity": 1024, "__lightvm_output_slots": 7}

    def test_target_geometry_and_compiled_limits(self):
        small, large = ram_report(self.symbols(264)), ram_report(self.symbols())
        self.assertEqual(large["remaining_bytes_after_reservations"] - small["remaining_bytes_after_reservations"], 256 * 1024)
        symbols = self.symbols()
        symbols["__lightvm_vm_heap"] *= 2
        self.assertEqual(ram_report(symbols)["remaining_bytes_after_reservations"], large["remaining_bytes_after_reservations"] - 65536)
        symbols["__lightvm_pixel_capacity"] = 175
        self.assertEqual(ram_report(symbols)["pixel_driver_max"], 7 * (175 * 4 + 256))

    def test_ram_code_scratch_and_stacks_are_included(self):
        symbols = self.symbols()
        expected = ram_report(symbols)["remaining_bytes_after_reservations"]
        symbols["__end__"] += 1024
        symbols["__scratch_x_end__"] += 256
        symbols["__StackOneBottom"] -= 512
        self.assertEqual(ram_report(symbols)["remaining_bytes_after_reservations"], expected - 1792)
        symbols["__lightvm_vm_heap"] = 520 * 1024
        self.assertLess(ram_report(symbols)["remaining_bytes_after_reservations"], 0)

    def test_unused_sram_cannot_hide_a_small_or_overlapping_heap(self):
        symbols = self.symbols()
        symbols["__HeapLimit"] = symbols["__end__"] + 65536
        self.assertLess(ram_report(symbols)["remaining_bytes_after_reservations"], 0)
        symbols["__HeapLimit"] = symbols["__StackBottom"] + 256
        with self.assertRaisesRegex(ValueError, "heap/stack"):
            ram_report(symbols)


@unittest.skipUnless(shutil.which("cmake") or os.environ.get("CMAKE"), "CMake not installed")
class PicoBoardSelectionTests(unittest.TestCase):
    def select(self, *settings):
        return subprocess.run([os.environ.get("CMAKE", "cmake"), *settings, "-P",
                               str(ROOT / "platforms/pico/select_board.cmake")],
                              capture_output=True, text=True,
                              env={k: v for k, v in os.environ.items() if k not in ("PICO_BOARD", "PICO_PLATFORM")})

    def test_supported_boards_and_conflicts(self):
        self.assertEqual(self.select().returncode, 0)
        for board in ("pico", "pico_w", "pico2_w"):
            with self.subTest(board=board):
                self.assertEqual(self.select(f"-DPICO_BOARD={board}").returncode, 0)
        for settings, message in (
            (("-DPICO_BOARD=unknown",), "Unsupported PICO_BOARD"),
            (("-DPICO_BOARD=pico2_w", "-DPICO_PLATFORM=rp2350-riscv"), "requires PICO_PLATFORM=rp2350-arm-s"),
            (("-DPICO_BOARD=pico2_w", "-DLIGHTVM_PICO_BOARD=pico_w"), "fresh build directory"),
        ):
            result = self.select(*settings)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(message, result.stderr)


if __name__ == "__main__":
    unittest.main()
