"""Serial connection policy must preserve Pico defaults and ESP32 program state."""
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from client import SerialTransport


class FakeSerial:
    def __init__(self, port=None, baudrate=115200, **kwargs):
        self.port = port
        self.dtr = True
        self.rts = True
        self.open_state = (port, self.dtr, self.rts) if port is not None else None
        self.incoming = [b"ROM boot output", b"stale reply"]
        self.reads = 0

    def open(self):
        self.open_state = (self.port, self.dtr, self.rts)

    def close(self):
        pass

    def read(self, size):
        self.reads += 1
        return self.incoming.pop(0) if self.incoming else b""


class SerialPolicyTests(unittest.TestCase):
    def test_esp32_lines_are_deasserted_before_open(self):
        with patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=FakeSerial)}):
            transport = SerialTransport("esp32-port", no_reset=True)
        self.assertEqual(transport.serial.open_state, ("esp32-port", False, False))
        self.assertEqual(transport.serial.reads, 3)
        self.assertFalse(transport.serial.incoming)

    def test_pico_default_control_lines_are_preserved(self):
        with patch.dict(sys.modules, {"serial": types.SimpleNamespace(Serial=FakeSerial)}):
            transport = SerialTransport("pico-port")
        self.assertEqual(transport.serial.open_state, ("pico-port", True, True))
        self.assertEqual(transport.serial.reads, 0)
