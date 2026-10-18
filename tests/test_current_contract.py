"""Release compatibility preserves programs; retired encodings never execute."""
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
from bytecode_fixtures import ROOT, color, program, PREFIX
sys.path.insert(0, str(ROOT / 'tools'))
from client import Client, ProcessTransport, ProtocolError, FRAME, STOP, frame
from test_configuration import config
from lightvm.release import VERSION, parse_release


def manifest_variant(raw, **changes):
    raw = bytearray(raw)
    size = struct.unpack_from('<I', raw, 76)[0]
    manifest = json.loads(raw[-size:])
    manifest.update(changes)
    metadata = json.dumps(manifest, separators=(',', ':')).encode()
    raw[-size:] = metadata
    struct.pack_into('<I', raw, 76, len(metadata))
    return raw


class CurrentContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.storage = Path(self.temp.name) / 'store.bin'
        self.receiver = Path(os.environ.get('LIGHTVM_RECEIVER', ROOT / 'build/host/lightvm'))
        self.transport = ProcessTransport(self.receiver, self.storage)
        self.client = Client(self.transport)
        self.client.configure(config())
        self.client.upload(color(37))
        self.generation = self.client.program_state()['generation']

    def tearDown(self):
        self.transport.close()
        self.temp.cleanup()

    def assert_rejected(self, candidate):
        with self.assertRaises(ProtocolError):
            self.client.save_program('obsolete', candidate)
        self.assertEqual(self.client.program_state()['generation'], self.generation)
        self.client.require_ok(self.client.command(FRAME, bytes(4)))
        self.assertEqual(bytes(self.client.pixel_snapshot), bytes([37, 2, 3]) * 5)

    def test_binary_layout_and_animation_bindings_remain_checked(self):
        original = color()
        for layout in (1, 2, 3, 99):
            raw = bytearray(original); struct.pack_into('<I', raw, 4, layout)
            with self.subTest(layout=layout): self.assert_rejected(raw)
        self.assert_rejected(manifest_variant(original, schema='lightvm.program/11'))
        for profile in (0, 1, 2, 4):
            raw = bytearray(original); struct.pack_into('<I', raw, 72, profile)
            with self.subTest(profile=profile): self.assert_rejected(raw)
        self.assert_rejected(manifest_variant(original, profile='layer/1'))

    def test_minimum_release_is_enforced_without_replacing_running_program(self):
        (major, minor, patch), _, _ = parse_release(VERSION)
        for required in (f'{major}.{minor}.{patch+1}', f'{major}.{minor+1}', str(major+1), '', '1.02', '1.0.0-01', None):
            with self.subTest(required=required):
                self.assert_rejected(manifest_variant(color(), requires=required))

    def test_compatible_programs_ignore_compiler_release_and_additive_metadata(self):
        for required in ('1', '1.0', '1.0.0', '1.0.0+build.123', '1.0.0-alpha.2'):
            with self.subTest(required=required):
                raw = manifest_variant(color(37), requires=required,
                                       compiler_version='1.25.7+development', notes={'future': True})
                self.client.save_program('compatible', raw)
                self.client.activate('compatible')
                self.client.require_ok(self.client.command(FRAME, bytes(4)))
                self.assertEqual(bytes(self.client.pixel_snapshot), bytes([37, 2, 3]) * 5)

    def test_removed_resource_and_native_encodings_cannot_claim_current_schema(self):
        original = program(PREFIX + '@render\ndef draw(ctx,runtime,pixels):\n pixels.point(0,rgb(1,2,3),128)\n')
        regs, code, colors, functions, resources, expressions, calls = struct.unpack_from('<7I', original, 8)
        resource_offset = 96 + regs*8 + code*8 + colors*4 + functions*24
        call_offset = resource_offset + resources*24 + expressions*16
        self.assertGreater(calls, 0)
        for kind in (3, 4, 5, 6, 9, 10, 13):
            raw = bytearray(original); struct.pack_into('<I', raw, resource_offset, kind)
            with self.subTest(kind=kind): self.assert_rejected(raw)
        for binding in (1, 2, *range(18, 27)):
            raw = bytearray(original); struct.pack_into('<I', raw, call_offset, binding)
            with self.subTest(native=binding): self.assert_rejected(raw)

    def test_removed_management_export_and_command_are_rejected(self):
        for op in ('program_parameters_get', 'program_parameters_set'):
            with self.assertRaises(ProtocolError): self.client.manage(op)
        self.assertEqual(self.client.command(5).code, 1)
        with self.assertRaises(ProtocolError):
            self.client.manage('program_activate', name='missing', export='run')
        state = self.client.program_state()
        self.assertNotIn('values', state); self.assertNotIn('parameter_revision', state)
        description = self.client.describe()
        self.assertEqual(description['version'], VERSION)
        for key in ('control_abi', 'bytecode_format', 'legacy_animation_api', 'parameter_limit',
                    'program_abis', 'program_schemas', 'program_profiles', 'bytecode_formats', 'native_drawing_api'):
            self.assertNotIn(key, description)

    def test_previous_wire_magic_is_ignored_and_current_decoder_recovers(self):
        obsolete = b'WASM' + frame(6, 600)[4:]
        self.transport.write(obsolete)
        self.assertEqual(self.client.program_state()['generation'], self.generation)
        self.assertEqual(self.client.command(1).code, 0)

    def test_all_canonical_examples_run_directly_and_as_layers(self):
        self.client.configure(config(20))
        for source in sorted((ROOT / 'examples/animations').glob('*.py')):
            with self.subTest(example=source.stem):
                raw = program(source.read_text())
                self.client.clear_composition(output='lights')
                self.client.save_program('example', raw)
                self.client.activate('example', output='lights')
                self.client.require_ok(self.client.command(STOP, output='lights'))
                self.client.require_ok(self.client.command(FRAME, struct.pack('<I', 17), output='lights'))
                self.assertEqual(len(self.client.pixel_snapshot), 60)
                self.client.configure_composition([{'id':'effect','program':'example'}], output='lights')
                self.client.require_ok(self.client.command(STOP, output='lights'))
                self.client.require_ok(self.client.command(FRAME, struct.pack('<I', 17), output='lights'))
                self.assertEqual(len(self.client.snapshot(output='lights', layer='effect')[1]), 80)

    def test_obsolete_directory_formats_are_preserved_in_either_bank(self):
        self.client.save_program('seed', color())
        self.transport.close()
        original = self.storage.read_bytes()
        for version in (0, 2, 3):
            for bank in (0, 256*1024):
                with self.subTest(version=version, bank=bank):
                    raw = bytearray(original)
                    # Keep exactly one committed directory, preserving its current CRC structure.
                    directory = bytearray(raw[:1024])
                    struct.pack_into('<I', directory, 4, version)
                    checksum = zlib.crc32(directory[:1020])
                    struct.pack_into('<I', directory, 1020, checksum)
                    raw[:4096] = b'\xff'*4096
                    raw[256*1024:256*1024+4096] = b'\xff'*4096
                    raw[bank:bank+1024] = directory
                    commit = struct.pack('<III', 0x54494d43, struct.unpack_from('<I', directory, 8)[0], checksum)
                    raw[bank+1024:bank+1040] = commit + struct.pack('<I', zlib.crc32(commit))
                    self.storage.write_bytes(raw)
                    self.transport = ProcessTransport(self.receiver, self.storage)
                    self.client = Client(self.transport)
                    self.assertIn('reprovision', self.client.describe()['boot_error'])
                    self.assertFalse(self.client.describe()['storage_available'])
                    with self.assertRaises(ProtocolError): self.client.save_program('new', color())
                    self.assertEqual(self.client.command(1).code, 0)
                    self.transport.close()
                    self.assertEqual(self.storage.read_bytes(), raw)
