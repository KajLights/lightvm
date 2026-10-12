"""Configuration and persistent library tests against the real engine and wire protocol."""
import copy
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
import wasmtime

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from client import (Client, ProcessTransport, ProtocolError, MANAGE, OBJECT_BEGIN,
                    OBJECT_CHUNK, OBJECT_FINISH, OBJECT_CANCEL, FRAME, RESET, RUN, PLAY)


def wasm(text):
    return bytes(wasmtime.wat2wasm(text))


def config(count=5, pin="GP16", startup=None):
    result = {"schema": "lightvm.config/1", "devices": [
        {"id": "lights", "type": "ws2812b", "config": {
            "pin": pin, "count": count, "order": "GRB", "brightness_limit": 32}}
    ], "targets": {"A": {"device": "lights"}}}
    if startup:
        result["startup"] = {"procedure": startup, "target": "A"}
    return result


def padded_module(size):
    def leb(value):
        output = bytearray()
        while value > 127:
            output.append((value & 127) | 128)
            value >>= 7
        output.append(value)
        return output
    for length in range(size - 15, size):
        data = b"\0asm\1\0\0\0\0" + leb(length) + bytes(length)
        if len(data) == size:
            return data
    raise AssertionError("could not construct padded module")


class ConfigurationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.storage = Path(self.temp.name) / "store.bin"
        self.receiver = Path(os.environ.get("LIGHTVM_RECEIVER", ROOT / "build/host/lightvm"))
        self.transport = None
        self.restart()

    def tearDown(self):
        if self.transport:
            self.transport.close()
        self.temp.cleanup()

    def restart(self):
        if self.transport:
            self.transport.close()
        self.transport = ProcessTransport(self.receiver, self.storage)
        self.output = bytearray()
        self.client = Client(self.transport, output=self.output.extend)

    def raw_manage(self, operation, **fields):
        return self.client.command(MANAGE, json.dumps({"op": operation, **fields}).encode())

    def test_blank_boot_and_missing_target(self):
        description = self.client.describe()
        self.assertTrue(description["storage_available"])
        self.assertEqual(description["storage_bytes"], 512 * 1024)
        self.assertEqual(description["active_revision"], 0)
        self.assertEqual(description["saved_revision"], 0)
        self.assertIn("GP16", description["pins"])
        self.assertNotIn("GP23", description["pins"])
        self.assertEqual(self.client.status_fields()["pixels"], "0")
        self.assertEqual(self.client.status_fields()["mode"], "idle")
        self.client.upload(wasm('(module (func (export "run") (result i32) i32.const 42))'))
        self.assertEqual(self.client.command(RUN).value, 42)
        self.assertEqual(self.client.command(PLAY).code, 4)
        self.assertIn("target A", self.client.command(FRAME, bytes(4)).message)

    def test_apply_changes_runtime_geometry_and_unloads(self):
        self.client.configure(config())
        self.client.upload(wasm('(module (import "led" "pixel_count" (func $n (result i32))) '
                                '(func (export "run") (result i32) call $n))'))
        self.assertEqual(self.client.command(RUN).value, 5)
        changed = config(count=7, pin="GP18")
        changed["devices"][0]["config"]["brightness_limit"] = 0
        changed["devices"][0]["config"]["order"] = "BGR"
        self.client.configure(changed)
        fields = self.client.status_fields()
        self.assertEqual((fields["pin"], fields["pixels"], fields["brightness"], fields["heap"]),
                         ("18", "7", "0", "0"))
        self.assertEqual(self.client.get_config(), changed)
        self.assertEqual(self.client.command(RUN).code, 4)
        self.restart()
        self.assertEqual(self.client.status_fields()["pixels"], "0")

    def test_invalid_config_retains_output_and_instance(self):
        original = config()
        self.client.configure(original)
        self.client.upload(wasm('(module (func (export "run") (result i32) i32.const 9))'))
        for field, value in [("count", 0), ("count", -1), ("count", 1.5), ("count", True),
                             ("count", 2**32), ("pin", "GP23"), ("order", "RRG"),
                             ("brightness_limit", 256), ("unknown", 1)]:
            candidate = copy.deepcopy(original)
            candidate["devices"][0]["config"][field] = value
            with self.assertRaises(ProtocolError):
                self.client.configure(candidate)
            self.assertEqual(self.client.get_config(), original)
            self.assertEqual(self.client.command(RUN).value, 9)
        candidate = copy.deepcopy(original)
        candidate["devices"][0]["type"] = "gpio"
        with self.assertRaises(ProtocolError):
            self.client.configure(candidate)
        candidate = copy.deepcopy(original)
        candidate["devices"].append(copy.deepcopy(candidate["devices"][0]))
        with self.assertRaises(ProtocolError):
            self.client.configure(candidate)
        with self.assertRaises(ProtocolError):
            self.client.upload_object("config", b'{"schema":"lightvm.config/1","devices":[],"devices":[]}')
        with self.assertRaises(ProtocolError):
            self.client.upload_object("config", b'{"schema":"lightvm.config/1","devices":[],"devi\\u0063es":[]}')
        self.assertEqual(self.client.get_config(), original)

    def test_stale_apply_and_save_revisions(self):
        self.client.upload_object("config", json.dumps(config()).encode())
        result = self.raw_manage("config_apply", expected_revision=99)
        self.assertEqual(result.code, 7)
        self.assertEqual(json.loads(self.client.data_reply)["error"], "stale_revision")
        self.client.manage("config_apply", expected_revision=0)
        self.assertEqual(self.raw_manage("config_apply", expected_revision=0).code, 7)
        self.client.save_config()
        self.assertEqual(self.raw_manage("config_save", expected_revision=1, expected_saved_revision=0).code, 7)

    def test_save_and_reboot_startup_without_host(self):
        data = wasm((ROOT / "samples/rainbow.wat").read_text())
        self.client.save_procedure("rainbow", data)
        self.client.configure(config(startup="rainbow"), save=True)
        self.assertEqual(self.client.status_fields()["mode"], "idle")
        self.assertEqual(self.client.get_config(saved=True), config(startup="rainbow"))
        self.restart()
        fields = self.client.status_fields()
        self.assertEqual((fields["mode"], fields["pixels"]), ("wasm", "5"))
        self.assertEqual(self.client.describe()["active_procedure"], "rainbow")
        self.assertEqual(self.client.describe()["boot_error"], "")
        self.assertEqual(self.client.get_procedure("rainbow"), data)
        self.client.command(RESET)
        self.assertEqual(self.client.status_fields()["heap"], "0")
        self.assertEqual(len(self.client.procedures()), 1)
        self.restart()
        self.assertEqual(self.client.status_fields()["mode"], "wasm")

    def test_missing_startup_reference_rejected(self):
        self.client.upload_object("config", json.dumps(config(startup="missing")).encode())
        self.assertEqual(self.raw_manage("config_validate").code, 7)
        self.assertIn("startup.procedure", self.client.data_reply.decode())
        self.assertEqual(self.client.status_fields()["pixels"], "0")

    def test_incompatible_startup_is_idle_and_repairable(self):
        self.client.save_procedure("bad", wasm('(module (func (export "run")))'))
        self.client.configure(config(startup="bad"), save=True)
        self.restart()
        self.assertEqual(self.client.status_fields()["mode"], "idle")
        self.assertIn("function", self.client.describe()["boot_error"])
        self.assertEqual(self.client.status_fields()["heap"], "0")
        self.client.configure(config())
        self.assertEqual(self.client.describe()["boot_error"], "")
        self.client.save_config()
        self.restart()
        self.assertEqual(self.client.status_fields()["pixels"], "5")

    def test_startup_timeout_and_render_trap(self):
        for text in ['(module (func $s (loop $l br $l)) (start $s) (func (export "render") (param i32)))',
                     '(module (func (export "render") (param i32) unreachable))']:
            self.client.save_procedure("bad", wasm(text))
            self.client.configure(config(startup="bad"), save=True)
            self.restart()
            fields = self.client.status_fields()
            self.assertEqual(fields["mode"], "idle")
            description = self.client.describe()
            self.assertTrue(description["boot_error"] or description["animation_error"])
            self.assertEqual(self.client.command(RESET).code, 0)

    def test_library_replacement_preserves_resident_instance(self):
        counter = wasm((ROOT / "samples/counter.wat").read_text())
        self.client.save_procedure("counter", counter)
        self.client.activate("counter", action="run")
        self.assertEqual(self.client.command(RUN).value, 2)
        old_crc = self.client.describe()["active_crc32"]
        replacement = wasm('(module (func (export "run") (result i32) i32.const 77))')
        self.client.save_procedure("counter", replacement)
        self.assertEqual(self.client.command(RUN).value, 3)
        self.assertEqual(self.client.describe()["active_crc32"], old_crc)
        self.assertEqual(self.client.get_procedure("counter"), replacement)
        self.assertEqual(self.client.activate("counter", action="run").value, 77)
        self.restart()
        self.assertEqual(self.client.status_fields()["mode"], "idle")
        self.assertEqual(self.client.activate("counter", action="run").value, 77)

    def test_references_block_deletion(self):
        data = wasm((ROOT / "samples/rainbow.wat").read_text())
        self.client.save_procedure("effect", data)
        self.client.configure(config(startup="effect"), save=True)
        with self.assertRaisesRegex(ProtocolError, "referenced"):
            self.client.delete_procedure("effect")
        self.client.configure({"schema": "lightvm.config/1", "devices": []})
        with self.assertRaisesRegex(ProtocolError, "referenced"):
            self.client.delete_procedure("effect")
        self.client.save_config()
        self.client.delete_procedure("effect")
        self.assertEqual(self.client.procedures(), [])

    def test_active_runtime_blocks_deletion(self):
        self.client.save_procedure("one", wasm('(module (func (export "run")))'))
        self.client.activate("one", action="run")
        with self.assertRaisesRegex(ProtocolError, "referenced"):
            self.client.delete_procedure("one")
        self.client.command(RESET)
        self.client.delete_procedure("one")

    def test_catalog_count_and_total_capacity(self):
        description = self.client.describe()
        data = padded_module(description["module_limit"])
        saved = 0
        for i in range(8):
            try:
                self.client.save_procedure(f"p{i}", data)
                saved += 1
            except ProtocolError:
                break
        self.assertGreaterEqual(saved, 7)
        self.assertLessEqual(saved, 8)
        self.assertEqual(len(self.client.procedures()), saved)
        if saved == 8:
            with self.assertRaises(ProtocolError):
                self.client.save_procedure("overflow", wasm('(module)'))
        else:
            self.assertLess(self.client.describe()["free_bytes"], len(data))
        self.restart()
        self.assertEqual(self.client.get_procedure("p0"), data)

    def test_staged_upload_retries_crc_cancellation_and_busy(self):
        data = wasm('(module (func (export "run")))')
        metadata = {"kind": "procedure", "name": "p", "size": len(data), "crc32": zlib.crc32(data)}
        self.assertEqual(self.client.command(OBJECT_BEGIN, json.dumps(metadata).encode()).code, 0)
        self.assertEqual(self.client.command(OBJECT_BEGIN, json.dumps(metadata).encode()).code, 9)
        chunk = bytes(4) + data[:8]
        self.assertEqual(self.client.command(OBJECT_CHUNK, chunk).value, 8)
        self.assertEqual(self.client.command(OBJECT_CHUNK, chunk).value, 8)
        self.assertEqual(self.client.command(OBJECT_CHUNK, bytes(4) + bytes(8)).code, 2)
        self.assertEqual(self.client.command(OBJECT_FINISH).code, 2)
        self.assertEqual(self.client.command(OBJECT_CHUNK, struct.pack("<I", 8) + data[8:]).value, len(data))
        self.assertEqual(self.client.command(OBJECT_FINISH).code, 0)
        self.assertEqual(self.client.procedures(), [])
        self.client.command(OBJECT_CANCEL)
        self.assertEqual(self.raw_manage("procedure_save", name="p", expected_revision=0).code, 2)
        metadata["crc32"] ^= 1
        self.client.command(OBJECT_BEGIN, json.dumps(metadata).encode())
        self.client.command(OBJECT_CHUNK, bytes(4) + data)
        self.assertEqual(self.client.command(OBJECT_FINISH).code, 2)
        with self.assertRaises(ProtocolError):
            self.client.upload_object("procedure", bytes(8), "bad")
        self.assertEqual(self.client.procedures(), [])

    def test_upload_and_save_do_not_execute_start_function(self):
        data = wasm('(module (func $s unreachable) (start $s) (func (export "run")))')
        self.client.save_procedure("trap", data)
        self.assertEqual(self.client.status_fields()["heap"], "0")
        with self.assertRaises(ProtocolError):
            self.client.activate("trap", action="run")
        self.assertEqual(self.client.status_fields()["heap"], "0")
        self.assertEqual(self.client.get_procedure("trap"), data)

    def test_repeated_activation_releases_memory(self):
        self.client.configure(config())
        data = wasm((ROOT / "samples/rainbow.wat").read_text())
        self.client.save_procedure("p", data)
        live = None
        for _ in range(12):
            self.client.activate("p")
            heap = self.client.status_fields()["heap"]
            if live is None:
                live = heap
            self.assertEqual(heap, live)
        self.client.command(RESET)
        self.assertEqual(self.client.status_fields()["heap"], "0")

    def test_corrupt_latest_snapshot_recovers_previous(self):
        one = wasm('(module (func (export "run") (result i32) i32.const 1))')
        two = wasm('(module (func (export "run") (result i32) i32.const 2))')
        self.client.save_procedure("p", one)
        self.client.save_procedure("p", two)
        self.transport.close()
        self.transport = None
        with self.storage.open("r+b") as stream:
            stream.seek(256 * 1024 + 4096)
            stream.write(b"bad!")
        self.restart()
        self.assertEqual(self.client.get_procedure("p"), one)
        self.assertEqual(self.client.describe()["catalog_revision"], 1)

    def test_corrupt_store_is_idle_and_can_be_repaired(self):
        self.client.save_procedure("p", wasm('(module)'))
        self.transport.close()
        self.transport = None
        with self.storage.open("r+b") as stream:
            stream.seek(0)
            stream.write(b"bad!")
        self.restart()
        self.assertEqual(self.client.status_fields()["mode"], "idle")
        self.assertIn("storage", self.client.describe()["boot_error"])
        self.client.save_procedure("repair", wasm('(module)'))
        self.restart()
        self.assertEqual(self.client.describe()["boot_error"], "")
        self.assertEqual(self.client.get_procedure("repair"), wasm('(module)'))

    def test_saved_missing_reference_after_external_corruption(self):
        data = wasm((ROOT / "samples/rainbow.wat").read_text())
        self.client.save_procedure("p", data)
        self.client.configure(config(startup="p"), save=True)
        # Replace only the catalog identifier, recomputing directory/commit CRCs,
        # to model a structurally valid imported snapshot with a broken reference.
        self.transport.close()
        self.transport = None
        raw = bytearray(self.storage.read_bytes())
        base = 256 * 1024
        raw[base+32:base+65] = b"q" + bytes(32)
        crc = zlib.crc32(raw[base:base+1020])
        struct.pack_into("<I", raw, base+1020, crc)
        struct.pack_into("<I", raw, base+1024+8, crc)
        struct.pack_into("<I", raw, base+1024+12, zlib.crc32(raw[base+1024:base+1024+12]))
        self.storage.write_bytes(raw)
        self.restart()
        self.assertEqual(self.client.status_fields()["mode"], "idle")
        self.assertIn("startup.procedure", self.client.describe()["boot_error"])

    def test_multi_frame_readback_and_configuration_size_limit(self):
        data = padded_module(2000)
        self.client.save_procedure("big", data)
        self.assertEqual(self.client.get_procedure("big"), data)
        raw = json.dumps(config()).encode() + bytes([32]) * 1000
        self.client.upload_object("config", raw)
        self.client.manage("config_apply", expected_revision=0)
        self.client.save_config()
        self.restart()
        self.assertEqual(self.client.get_config(), config())
        with self.assertRaises(ValueError):
            self.client.upload_object("config", b" " * 8193)
