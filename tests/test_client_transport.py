"""Serial connection policy must preserve Pico defaults and ESP32 program state."""
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from client import Client, ProtocolError, Reply, SerialTransport


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


class OperationWaitTests(unittest.TestCase):
    def test_finish_upload_wait_has_a_deadline_and_keeps_operation_identity(self):
        client = Client(None)
        client.data_reply = b'{"operation":{"id":42,"boot_id":"boot-7","terminal":false}}'
        with patch.object(client, 'command', return_value=Reply(0, 42, 'accepted')), \
                patch('client.time.monotonic', side_effect=[0, 121]):
            with self.assertRaisesRegex(ProtocolError, '42.*boot-7'):
                client.finish_upload(require_ok=False)

    def test_finish_upload_preserves_optional_terminal_error_inspection(self):
        client = Client(None)
        client.data_reply = b'{"operation":{"id":42,"terminal":true,"code":7,"value":0,"error":"cancelled","stage":"cancelled"}}'
        with patch.object(client, 'command', return_value=Reply(0, 42, 'accepted')):
            self.assertEqual(client.finish_upload(require_ok=False).code, 7)
            with self.assertRaisesRegex(ProtocolError, 'cancelled'):
                client.finish_upload()
