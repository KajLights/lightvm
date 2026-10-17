"""Bytecode admission and the unchanged framed upload/command protocol."""
import os
from pathlib import Path
import struct
import sys
import unittest
import zlib
from bytecode_fixtures import ROOT, color, fault
sys.path.insert(0, str(ROOT / 'tools'))
from client import BEGIN, CHUNK, FINISH, FRAME, RESET, STATUS, OBJECT_CANCEL, Client, ProcessTransport, ProtocolError
from test_configuration import config

class ReceiverTests(unittest.TestCase):
    def setUp(self):
        self.transport = ProcessTransport(Path(os.environ.get('LIGHTVM_RECEIVER', ROOT / 'build/host/lightvm')))
        self.client = Client(self.transport)
        self.client.configure(config())

    def tearDown(self):
        self.transport.close()

    def test_upload_retries_out_of_order_crc_and_reset(self):
        data = color()
        self.assertEqual(self.client.command(BEGIN, struct.pack('<II',len(data),zlib.crc32(data))).code, 0)
        first = bytes(4) + data[:100]
        self.assertEqual(self.client.command(CHUNK,first).value, 100)
        self.assertEqual(self.client.command(CHUNK,first).value, 100)
        self.assertEqual(self.client.command(CHUNK,struct.pack('<I',101)+data[100:]).code, 2)
        self.assertEqual(self.client.command(FINISH).code, 2)
        self.assertEqual(self.client.command(CHUNK,struct.pack('<I',100)+data[100:]).code, 0)
        self.assertEqual(self.client.command(FINISH).code, 0)
        self.assertEqual(self.client.command(FRAME,bytes(4)).code, 0)
        self.assertEqual(bytes(self.client.pixel_snapshot), bytes([1,2,3])*5)
        self.client.command(BEGIN,struct.pack('<II',len(data),zlib.crc32(data)^1))
        self.client.command(CHUNK,bytes(4)+data)
        self.assertEqual(self.client.command(FINISH).code, 2)
        self.assertEqual(self.client.status_fields()['ready'],'1')
        self.client.command(RESET)
        self.assertEqual(self.client.status_fields()['heap'],'0')

    def test_invalid_packages_are_rejected_without_execution(self):
        original = color()
        code = 96 + struct.unpack_from('<I',original,8)[0]*8
        variants = [b'\0asm\1\0\0\0', original[:-1], original+b'x']
        for offset, value in ((4,99),(8,0xffffffff),(20,0),(32,257),(80,1),(code,255)):
            data=bytearray(original);struct.pack_into('<I',data,offset,value);variants.append(data)
        # Typed destination and register indices must be checked before dispatch.
        data=bytearray(original);data[code+8+1]=255;variants.append(data)
        for data in variants:
            with self.subTest(data=bytes(data[:40])):
                self.client.command(BEGIN,struct.pack('<II',len(data),zlib.crc32(data)))
                for offset in range(0,len(data),508):
                    self.client.command(CHUNK,struct.pack('<I',offset)+data[offset:offset+508])
                self.assertEqual(self.client.command(FINISH).code,3)
                self.assertEqual(self.client.status_fields()['ready'],'0')
        self.client.upload(original)
        self.assertEqual(self.client.command(FRAME,bytes(4)).code,0)


    def test_failed_or_cancelled_replacement_preserves_live_program(self):
        self.client.upload(color(31))
        generation = self.client.program_state()['generation']
        for candidate in (fault(), b'LBC1' + bytes(12)):
            with self.assertRaises(ProtocolError):
                self.client.upload(candidate)
            self.assertEqual(self.client.program_state()['generation'], generation)
            self.client.command(FRAME, bytes(4))
            self.assertEqual(self.client.pixel_snapshot[0], 31)
        data = color(99)
        self.client.require_ok(self.client.command(BEGIN, struct.pack('<II', len(data), zlib.crc32(data))))
        self.client.require_ok(self.client.command(CHUNK, bytes(4) + data[:100]))
        self.client.require_ok(self.client.command(OBJECT_CANCEL))
        self.assertEqual(self.client.program_state()['generation'], generation)
        self.client.command(FRAME, bytes(4))
        self.assertEqual(self.client.pixel_snapshot[0], 31)

    def test_fault_leaves_protocol_usable(self):
        for data, expected in ((fault(),4),):
            with self.assertRaises(ProtocolError):self.client.upload(data)
            self.assertEqual(self.client.command(STATUS).code,0)
            self.client.upload(color(12))
            self.assertEqual(self.client.command(FRAME,bytes(4)).code,0)
            self.assertEqual(self.client.pixel_snapshot[0],12)

    def test_run_is_rejected_and_command_payload_validation_is_unchanged(self):
        self.client.upload(color())
        self.assertEqual(self.client.command(5).code,1)
        self.assertEqual(self.client.command(10).code,1)
        self.assertEqual(self.client.command(FRAME,struct.pack('<I',1001)).code,1)
        self.assertEqual(self.client.command(RESET,b'x').code,1)
        limit=int(self.client.status_fields()['module_limit'])
        self.assertEqual(self.client.command(BEGIN,struct.pack('<II',limit+1,0)).code,2)
