"""Bytecode admission and the unchanged framed upload/command protocol."""
import os
import json
from pathlib import Path
import struct
import sys
import unittest
import zlib
from bytecode_fixtures import ROOT, color, fault, infinite, sample
sys.path.insert(0, str(ROOT / 'tools'))
from client import BEGIN, CHUNK, FINISH, FRAME, RESET, RUN, STATUS, Client, ProcessTransport
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
        self.assertEqual(self.client.status_fields()['ready'],'0')
        self.client.command(RESET)
        self.assertEqual(self.client.status_fields()['heap'],'0')

    def test_invalid_packages_are_rejected_without_execution(self):
        original = color()
        code = 64 + struct.unpack_from('<I',original,8)[0]*8
        variants = [b'\0asm\1\0\0\0', original[:-1], original+b'x']
        for offset, value in ((4,99),(8,0xffffffff),(20,1),(32,7),(40,1),(code,255)):
            data=bytearray(original);struct.pack_into('<I',data,offset,value);variants.append(data)
        # Typed destination and register indices must be checked before dispatch.
        data=bytearray(original);data[code+16+1]=255;variants.append(data)
        for data in variants:
            with self.subTest(data=bytes(data[:40])):
                self.client.command(BEGIN,struct.pack('<II',len(data),zlib.crc32(data)))
                for offset in range(0,len(data),508):
                    self.client.command(CHUNK,struct.pack('<I',offset)+data[offset:offset+508])
                self.assertEqual(self.client.command(FINISH).code,3)
                self.assertEqual(self.client.status_fields()['ready'],'0')
        self.client.upload(original)
        self.assertEqual(self.client.command(FRAME,bytes(4)).code,0)

    def test_manifest_must_match_parameter_registers(self):
        original=sample('color')
        length=struct.unpack_from('<I',original,36)[0]
        metadata=json.loads(original[-length:])
        for change in ('default','type','schema','count'):
            candidate=json.loads(json.dumps(metadata))
            if change=='default':candidate['parameters'][0]['default']=7
            elif change=='type':candidate['parameters'][0]['type']='int'
            elif change=='schema':candidate['schema']='lightvm.program/2'
            else:candidate['parameters']=[]
            manifest=json.dumps(candidate).encode()
            data=bytearray(original[:-length])+manifest
            struct.pack_into('<I',data,36,len(manifest))
            self.client.command(BEGIN,struct.pack('<II',len(data),zlib.crc32(data)))
            for at in range(0,len(data),508):self.client.command(CHUNK,struct.pack('<I',at)+data[at:at+508])
            self.assertEqual(self.client.command(FINISH).code,3)

    def test_fault_and_instruction_limit_leave_protocol_usable(self):
        for data, expected in ((fault(),4),(infinite(),5)):
            self.client.upload(data)
            self.assertEqual(self.client.command(FRAME,bytes(4)).code,expected)
            self.assertEqual(self.client.command(STATUS).code,0)
            self.client.upload(color(12))
            self.assertEqual(self.client.command(FRAME,bytes(4)).code,0)
            self.assertEqual(self.client.pixel_snapshot[0],12)

    def test_run_is_rejected_and_command_payload_validation_is_unchanged(self):
        self.client.upload(color())
        self.assertEqual(self.client.command(RUN).code,4)
        self.assertEqual(self.client.command(10).code,1)
        self.assertEqual(self.client.command(FRAME,struct.pack('<I',1001)).code,1)
        self.assertEqual(self.client.command(RESET,b'x').code,1)
        limit=int(self.client.status_fields()['module_limit'])
        self.assertEqual(self.client.command(BEGIN,struct.pack('<II',limit+1,0)).code,2)
