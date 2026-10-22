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
from bytecode_fixtures import color, sample, fault, padded_program, program

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from client import (Client, ProcessTransport, ProtocolError, MANAGE, OBJECT_BEGIN,
                    OBJECT_CHUNK, OBJECT_FINISH, OBJECT_CANCEL, FRAME, RESET, PLAY)


def config(count=5, pin="GP16", startup=None):
    result = {"schema": "lightvm.config", "outputs": [
        {"id": "lights", "type": "ws2812b", "config": {
            "pin": pin, "count": count, "order": "GRB", "brightness_limit": 32}}
    ]}
    if startup:
        result["outputs"][0]["startup"] = {"mode": "preset", "preset": startup}
    return result


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
        self.client = Client(self.transport)

    def raw_manage(self, operation, **fields):
        reply = self.client.command(MANAGE, json.dumps({"op": operation, **fields}).encode())
        if reply.code == 0 and self.client.data_reply.startswith(b'{"operation":'):
            state = json.loads(self.client.data_reply)["operation"]
            while not state["terminal"]:
                state = self.client.operation(state["boot_id"], state["id"])
            reply.code, reply.value, reply.message = state["code"], state["value"], state["error"]
        return reply

    def test_blank_boot_and_missing_target(self):
        description = self.client.describe()
        self.assertTrue(description["storage_available"])
        self.assertEqual(description["storage_bytes"], 512 * 1024)
        self.assertEqual(description["active_revision"], 0)
        self.assertIn("GP16", description["pins"])
        self.assertEqual(self.client.status_fields()["pixels"], "0")
        self.client.upload(color())
        self.assertEqual(self.client.command(5).code, 1)
        self.assertEqual(self.client.command(PLAY).code, 4)
        self.assertIn("unavailable", self.client.command(FRAME, bytes(4)).message)

    def test_apply_changes_runtime_geometry_and_unloads(self):
        self.client.configure(config())
        self.client.upload(color())
        self.assertEqual(self.client.command(FRAME, bytes(4)).value, 5)
        changed = config(count=7, pin="GP18")
        changed["outputs"][0]["config"]["brightness_limit"] = 0
        changed["outputs"][0]["config"]["order"] = "BGR"
        self.client.configure(changed)
        fields = self.client.status_fields()
        self.assertEqual((fields["pin"], fields["pixels"], fields["brightness"]), ("18", "7", "0"))
        output_heap = fields["heap"]
        self.client.command(RESET)
        self.assertEqual(self.client.status_fields()["heap"], output_heap)
        self.assertEqual(self.client.get_config(), changed)
        self.assertEqual(self.client.command(5).code, 1)
        self.restart()
        self.assertEqual(self.client.status_fields()["pixels"], "0")

    def test_invalid_config_retains_output_and_instance(self):
        original = config()
        self.client.configure(original)
        self.client.upload(color(9))
        for field, value in [("count", 0), ("count", -1), ("count", 1.5), ("count", True),
                             ("count", 2**32), ("pin", "GP23"), ("order", "RRG"),
                             ("brightness_limit", 256), ("unknown", 1)]:
            candidate = copy.deepcopy(original)
            candidate["outputs"][0]["config"][field] = value
            with self.assertRaises(ProtocolError):
                self.client.configure(candidate)
            self.assertEqual(self.client.get_config(), original)
            self.client.command(FRAME, bytes(4))
            self.assertEqual(self.client.pixel_snapshot[0], 9)
        candidate = copy.deepcopy(original)
        candidate["outputs"][0]["type"] = "gpio"
        with self.assertRaises(ProtocolError):
            self.client.configure(candidate)
        candidate = copy.deepcopy(original)
        candidate["outputs"].append(copy.deepcopy(candidate["outputs"][0]))
        with self.assertRaises(ProtocolError):
            self.client.configure(candidate)
        with self.assertRaises(ProtocolError):
            self.client.upload_object("config", b'{"schema":"lightvm.config","outputs":[],"outputs":[]}')
        with self.assertRaises(ProtocolError):
            self.client.upload_object("config", b'{"schema":"lightvm.config","outputs":[],"devi\\u0063es":[]}')
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


    def test_missing_startup_reference_rejected(self):
        self.client.upload_object("config", json.dumps(config(startup="missing")).encode())
        self.assertEqual(self.raw_manage("config_validate").code, 7)
        self.assertIn("startup", self.client.data_reply.decode())
        self.assertEqual(self.client.status_fields()["pixels"], "0")



    def test_library_replacement_preserves_resident_instance(self):
        self.client.configure(config())
        self.client.save_program("effect", color(7))
        self.client.activate("effect")
        self.client.command(FRAME, bytes(4))
        old_crc = self.client.describe()["active_crc32"]
        self.assertEqual(self.client.status_fields()["code_storage"],"flash")
        self.client.save_program("effect", color(77))
        self.assertEqual(self.client.status_fields()["code_storage"],"flash")
        # Multiple snapshot rotations must preserve the old active version.
        for i in range(3):
            self.client.save_program("other", color(i))
            self.client.command(FRAME, bytes(4))
            self.assertEqual(self.client.pixel_snapshot[0], 7)
            self.assertEqual(self.client.describe()["active_crc32"], old_crc)
        self.assertEqual(self.client.get_program("effect"), color(77))
        self.client.activate("effect")
        self.client.command(FRAME, bytes(4))
        self.assertEqual(self.client.pixel_snapshot[0], 77)
        self.client.save_config()
        self.restart()
        self.client.activate("effect")
        self.client.command(FRAME, bytes(4))
        self.assertEqual(self.client.pixel_snapshot[0], 77)




    def test_active_runtime_blocks_deletion(self):
        self.client.save_program("one", color())
        self.client.configure(config())
        self.client.activate("one")
        with self.assertRaisesRegex(ProtocolError, "referenced"):
            self.client.delete_program("one")
        self.client.command(RESET)
        self.client.delete_program("one")

    def test_catalog_count_and_total_capacity(self):
        description = self.client.describe()
        data = padded_program(min(27000,description["module_limit"]))
        saved = 0
        while True:
            try:
                self.client.save_program(f"p{saved}", data)
                saved += 1
            except ProtocolError:
                break
        self.assertGreater(saved, 8)
        self.assertEqual(len(self.client.programs()), saved)
        self.assertLess(self.client.describe()["free_bytes"], len(data) + 8192)
        self.restart()
        self.assertEqual(self.client.get_program("p0"), data)

    def test_staged_upload_retries_crc_cancellation_and_busy(self):
        data = color()
        metadata = {"kind": "program", "name": "p", "size": len(data), "crc32": zlib.crc32(data)}
        self.assertEqual(self.client.command(OBJECT_BEGIN, json.dumps(metadata).encode()).code, 0)
        self.assertEqual(self.client.command(OBJECT_BEGIN, json.dumps(metadata).encode()).code, 9)
        chunk = bytes(4) + data[:8]
        self.assertEqual(self.client.command(OBJECT_CHUNK, chunk).value, 8)
        self.assertEqual(self.client.command(OBJECT_CHUNK, chunk).value, 8)
        self.assertEqual(self.client.command(OBJECT_CHUNK, bytes(4) + bytes(8)).code, 2)
        self.assertEqual(self.client.command(OBJECT_FINISH).code, 2)
        self.assertEqual(self.client.command(OBJECT_CHUNK, struct.pack("<I", 8) + data[8:]).value, len(data))
        self.assertEqual(self.client.command(OBJECT_FINISH).code, 0)
        self.assertEqual(self.client.programs(), [])
        self.client.command(OBJECT_CANCEL)
        self.assertEqual(self.raw_manage("program_save", name="p", expected_revision=0).code, 2)
        metadata["crc32"] ^= 1
        self.client.command(OBJECT_BEGIN, json.dumps(metadata).encode())
        self.client.command(OBJECT_CHUNK, bytes(4) + data)
        self.assertEqual(self.client.command(OBJECT_FINISH).code, 2)
        with self.assertRaises(ProtocolError):
            self.client.upload_object("program", bytes(8), "bad")
        self.assertEqual(self.client.programs(), [])

    def test_upload_and_save_do_not_execute_start_function(self):
        data = fault()
        self.client.save_program("trap", data)
        self.assertEqual(self.client.status_fields()["heap"], "0")
        with self.assertRaises(ProtocolError):
            self.client.activate("trap")
        self.assertEqual(int(self.client.status_fields()["heap"]), self.client.describe()["preparation_memory_bytes"])
        self.assertEqual(self.client.get_program("trap"), data)

    def test_repeated_activation_releases_memory(self):
        self.client.configure(config())
        output_heap = self.client.status_fields()["heap"]
        data = sample("rainbow")
        self.client.save_program("p", data)
        live = None
        for _ in range(12):
            self.client.activate("p")
            heap = self.client.status_fields()["heap"]
            if live is None:
                live = heap
            self.assertEqual(heap, live)
        self.client.command(RESET)
        self.assertEqual(self.client.status_fields()["heap"], output_heap)

    def test_corrupt_latest_snapshot_recovers_previous(self):
        one = color(1)
        two = color(2)
        self.client.save_program("p", one)
        self.client.save_program("p", two)
        self.transport.close()
        self.transport = None
        with self.storage.open("r+b") as stream:
            stream.seek(256 * 1024)
            stream.write(b"bad!")
        self.restart()
        self.assertEqual(self.client.get_program("p"), one)
        self.assertEqual(self.client.describe()["catalog_revision"], 1)

    def test_corrupt_store_requires_explicit_reprovision(self):
        self.client.save_program("p", color())
        self.transport.close()
        self.transport = None
        with self.storage.open("r+b") as stream:
            stream.seek(0)
            stream.write(b"bad!")
        self.restart()
        self.assertEqual(self.client.status_fields()["mode"], "idle")
        self.assertIn("storage", self.client.describe()["boot_error"])
        original=self.storage.read_bytes()
        self.assertFalse(self.client.describe()['storage_available'])
        with self.assertRaises(ProtocolError):self.client.save_program("repair",color())
        self.assertEqual(self.storage.read_bytes(),original)
        # Explicitly reprovision only this isolated test store.
        self.transport.close();self.transport=None;self.storage.unlink()
        self.restart()
        self.client.save_program("repair",color())
        self.assertEqual(self.client.get_program("repair"),color())


    def test_multi_frame_readback_and_configuration_size_limit(self):
        data = padded_program(2000)
        self.client.save_program("big", data)
        self.assertEqual(self.client.get_program("big"), data)
        raw = json.dumps(config()).encode() + bytes([32]) * 1000
        self.client.upload_object("config", raw)
        self.client.manage("config_apply", expected_revision=0)
        self.client.save_config()
        self.restart()
        self.assertEqual(self.client.get_config(), config())
        with self.assertRaises(ValueError):
            self.client.upload_object("config", b" " * 8193)
