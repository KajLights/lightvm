"""Boot gesture and independent host persistence through the real protocol."""
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
import zlib
from bytecode_fixtures import color, sample, padded_program

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from client import Client, ProcessTransport, RESET


class BootSequenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.storage = Path(self.temp.name) / "store.bin"
        self.sidecar = Path(str(self.storage) + ".boot-sequence")
        self.receiver = Path(os.environ.get("LIGHTVM_RECEIVER", ROOT / "build/host/lightvm"))
        self.transport = None
        self.restart()
        self.supported = self.client.describe()["boot_sequence_supported"]

    def tearDown(self):
        if self.transport:
            self.transport.close()
        self.temp.cleanup()

    def restart(self, storage=True):
        if self.transport:
            self.transport.close()
        self.transport = ProcessTransport(self.receiver, self.storage if storage else None)
        self.client = Client(self.transport)

    def require_detector(self):
        if not self.supported:
            self.skipTest("Requires LIGHTVM_BOOT_SEQUENCE=ON")

    def test_capability_and_disabled_storage(self):
        description = self.client.describe()
        if self.supported:
            self.assertEqual(description["boot_sequence"], {"count": 1, "triggered": False, "error": ""})
            self.assertEqual(self.sidecar.stat().st_size, 8192)
        else:
            self.assertNotIn("boot_sequence", description)
            self.assertFalse(self.sidecar.exists())
        self.assertEqual(self.storage.stat().st_size, 512 * 1024)

    def test_three_power_ons_and_guest_reset(self):
        self.require_detector()
        for boot, count in enumerate((1, 2, 3, 3)):
            if boot:
                self.restart()
            state = self.client.describe()["boot_sequence"]
            self.assertEqual(state, {"count": count, "triggered": count == 3, "error": ""})
            self.client.command(RESET)
            self.assertEqual(self.client.describe()["boot_sequence"], state)

    def test_five_seconds_clear_counter_and_preserve_flag(self):
        self.require_detector()
        self.restart(); self.restart()
        self.assertTrue(self.client.describe()["boot_sequence"]["triggered"])
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            state = self.client.describe()["boot_sequence"]
            if state["count"] == 0:
                break
            time.sleep(0.05)
        self.assertEqual(state, {"count": 0, "triggered": True, "error": ""})
        self.restart()
        self.assertEqual(self.client.describe()["boot_sequence"]["count"], 1)
        self.assertFalse(self.client.describe()["boot_sequence"]["triggered"])

    def test_trigger_preserves_saved_startup_and_applies_recovery_policy(self):
        self.require_detector()
        module = color()
        self.client.save_program("startup", module)
        config = {"schema": "lightvm.config", "outputs": [
            {"id": "lights", "type": "ws2812b", "config": {"pin": "GP16", "count": 5}, "startup": {"mode": "preset", "preset": "startup"}}
        ]}
        hardware = {**config, 'outputs': [{k: v for k, v in config['outputs'][0].items() if k != 'startup'}]}
        self.client.configure(hardware)
        self.client.save_preset('startup', [{'id': 'main', 'program': 'startup', 'crc32': zlib.crc32(module)}])
        self.client.configure(config, save=True)
        snapshot = self.storage.read_bytes()
        for count in (2, 3, 3):
            self.restart()
            description = self.client.describe()
            self.assertEqual(description["boot_sequence"]["count"], count)
            recovery = description.get("recovery_supported", False) and count >= 3
            self.assertEqual(description["outputs"][0]["selected_preset"], None if recovery else "startup")
            self.assertEqual(description["boot_error"], "")
            self.assertEqual(self.client.status_fields()["mode"], "idle" if recovery else "transition")
            self.assertEqual(self.client.get_config(saved=True), config)
            self.assertEqual(self.client.get_program("startup"), module)
            self.assertEqual(self.storage.read_bytes(), snapshot)

    def test_missing_or_invalid_metadata_keeps_management_available(self):
        self.require_detector()
        self.restart(storage=False)
        state = self.client.describe()["boot_sequence"]
        self.assertEqual(state["count"], 0)
        self.assertFalse(state["triggered"])
        self.assertTrue(state["error"])
        self.transport.close(); self.transport = None
        self.sidecar.write_bytes(b"invalid length")
        self.restart()
        description = self.client.describe()
        self.assertTrue(description["boot_sequence"]["error"])
        self.assertEqual(description["boot_error"], "")
        self.assertTrue(description["storage_available"])


if __name__ == "__main__":
    unittest.main()
