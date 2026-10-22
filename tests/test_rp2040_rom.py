import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('rom_audit', Path(__file__).resolve().parents[1] / 'tools/audit_rp2040_rom.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def fixture(code):
    rom = bytearray(512)
    rom[0x13] = 3
    struct.pack_into('<HHH', rom, 0x14, 0x40, 0x50, 0x101)
    rom[0x40:0x44] = b'XX\x01\x01'
    rom[0x50:0x58] = b'SF\x80\x00SD\x80\x00'
    struct.pack_into('<32I', rom, 0x80, *([0x101] * 32))
    lines = []
    for address, words, instruction in code:
        raw = b''.join(int(word, 16).to_bytes(2, 'little') for word in words.split())
        rom[address:address + len(raw)] = raw
        lines.append(f'{address:x}: {words}\t{instruction}')
    return bytes(rom), '\n'.join(lines)


class RomStackAuditTests(unittest.TestCase):
    def test_nested_calls_count_both_frames(self):
        rom, text = fixture([(0x100, 'b500', 'push {lr}'), (0x102, 'f000 f805', 'bl 0x110'),
                             (0x106, 'bd00', 'pop {pc}'), (0x110, 'b510', 'push {r4, lr}'),
                             (0x112, 'bd10', 'pop {r4, pc}')])
        result = audit.audit(rom, text)
        self.assertEqual(result['entries']['XX']['maximum_bytes'], 12)

    def test_unknown_register_call_remains_unresolved(self):
        rom, text = fixture([(0x100, '4798', 'blx r3')])
        self.assertFalse(audit.audit(rom, text)['entries']['XX']['closed'])

    def test_mismatched_disassembly_is_rejected(self):
        rom, text = fixture([(0x100, '4770', 'bx lr')])
        with self.assertRaisesRegex(ValueError, 'does not match'):
            audit.audit(rom, text.replace('4770', 'b500'))

    def test_growing_stack_loop_is_unresolved(self):
        rom, text = fixture([(0x100, 'b500', 'push {lr}'), (0x102, 'e7fd', 'b.n 0x100')])
        self.assertFalse(audit.audit(rom, text)['entries']['XX']['closed'])

    def test_branch_alternatives_take_larger_stack(self):
        rom, text = fixture([(0x100, 'd001', 'beq.n 0x106'), (0x102, '4770', 'bx lr'),
                             (0x106, 'b510', 'push {r4, lr}'), (0x108, 'bd10', 'pop {r4, pc}')])
        self.assertEqual(audit.audit(rom, text)['entries']['XX']['maximum_bytes'], 8)

    def test_stack_register_write_is_unresolved(self):
        rom, text = fixture([(0x100, 'f380 8808', 'msr MSP, r0'), (0x104, '4770', 'bx lr')])
        self.assertFalse(audit.audit(rom, text)['entries']['XX']['closed'])

    def test_data_directive_is_not_treated_as_a_zero_stack_instruction(self):
        rom, text = fixture([(0x100, 'dead', '.short 0xdead'), (0x102, '4770', 'bx lr')])
        self.assertFalse(audit.audit(rom, text)['entries']['XX']['closed'])


if __name__ == '__main__':
    unittest.main()
