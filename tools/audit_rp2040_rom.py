#!/usr/bin/env python3
"""Audit direct Thumb stack paths in a captured RP2040 ROM.

This narrow check fails closed on unknown indirect branches and stack effects.
Identified B2 helpers have explicit, dump-hash-guarded contracts. It reports partial coverage, never certifies
an unanalysed ROM entry or an interrupt allowance. Use an objdump generated with
`-D -b binary -marm -Mforce-thumb` and the exact ROM dump that it describes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def audit(rom, disassembly):
    rom_hash = hashlib.sha256(rom).hexdigest()
    instructions = {}
    for line in disassembly.splitlines():
        match = re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{4}\s+)+)\s*([^\s]+)\s*(.*?)\s*(?:@.*)?$', line)
        if match:
            address, words, mnemonic, operands = match.groups()
            encoded = b''.join(int(word, 16).to_bytes(2, 'little') for word in words.split())
            if rom[int(address, 16):int(address, 16) + len(encoded)] != encoded:
                raise ValueError('disassembly does not match ROM at ' + address)
            op = mnemonic if mnemonic.startswith('.') else mnemonic.split('.')[0]
            instructions[int(address, 16)] = (len(words.split()) * 2, op, operands.split('@')[0].strip())
    cache, visiting = {}, set()
    branches, scratch_lr = {}, set()
    if rom_hash == 'ef07d54bdd259c7858483606118a3c704976801823c38ac5a2fcdffacd2d83fe':
        # Published B2 mufplib-double.S saves/restores r8-r11 in r4-r7 via
        # these two leaf helpers. They deliberately leave/consume 16 bytes.
        cache.update({0x349e: (16, {16}), 0x34aa: (0, {-16})})
        # memcpy/memset dispatch to a byte tail only after count < 8.
        # Every possible target is enumerated; none changes the stack.
        branches = {0x2688: range(0x268c, 0x26a9, 4), 0x271c: range(0x271e, 0x272d, 2)}
        # Float multiply saves LR in its opening PUSH, uses LR for the sign,
        # makes no calls, and returns through POP {r7, pc} on every path.
        scratch_lr = {0x2c16}

    def function(entry):
        if entry in cache:
            return cache[entry]
        if entry in visiting:
            raise ValueError(f'recursive call at {entry:#x}')
        visiting.add(entry)
        try:
            work, seen, maximum, returns = [(entry, 0)], set(), 0, set()
            while work:
                address, depth = work.pop()
                if (address, depth) in seen:
                    continue
                if depth < 0 or depth > 1024 or len(seen) > 20000:
                    raise ValueError(f'nonstandard or unbounded stack at {address:#x}, depth {depth}')
                seen.add((address, depth))
                maximum = max(maximum, depth)
                if address not in instructions:
                    raise ValueError(f'missing instruction at {address:#x}')
                size, op, arg = instructions[address]
                following = address + size
                if op in ('push', 'pop'):
                    registers = arg.strip('{}').split(',')
                    if any('-' in r for r in registers):
                        raise ValueError('register range not expanded')
                    depth += 4 * len(registers) * (1 if op == 'push' else -1)
                    if op == 'pop' and 'pc' in arg:
                        returns.add(depth)
                        continue
                elif op in ('sub', 'add') and arg.startswith('sp, #'):
                    delta = int(arg.split('#')[1], 0)
                    depth += delta if op == 'sub' else -delta
                elif op == 'bl':
                    target = int(arg.split()[0], 16)
                    child_max, child_returns = function(target)
                    maximum = max(maximum, depth + child_max)
                    for delta in child_returns:
                        work.append((following, depth + delta))
                    continue
                elif op in ('b', 'beq', 'bne', 'bcs', 'bcc', 'bmi', 'bpl', 'bvs', 'bvc', 'bhi', 'bls', 'bge', 'blt', 'bgt', 'ble'):
                    target = int(arg.split()[0], 16)
                    work.append((target, depth))
                    if op == 'b':
                        continue
                elif op == 'bx' and arg == 'lr':
                    returns.add(depth)
                    continue
                elif op == 'mov' and arg == 'pc, lr':
                    returns.add(depth)
                    continue
                elif address in branches and op == 'bx' and arg == 'r3':
                    work.extend((target, depth) for target in branches[address])
                    continue
                elif address in scratch_lr and op == 'mov' and arg == 'lr, r2':
                    pass
                elif (op in ('blx', 'bx', 'svc', 'bkpt', 'udf') or op.startswith('.') or
                      re.match(r'(?:sp|pc|lr)[!,]', arg) or
                      (op == 'msr' and re.match(r'(?:msp|psp|control),', arg, re.I))):
                    raise ValueError(f'unresolved control/stack operation at {address:#x}: {op} {arg}')
                maximum = max(maximum, depth)
                work.append((following, depth))
            if returns - {0}:
                raise ValueError(f'nonstandard return depths {returns} for {entry:#x}')
            cache[entry] = maximum, returns
            return cache[entry]
        finally:
            visiting.remove(entry)

    def table(offset):
        result = {}
        while int.from_bytes(rom[offset:offset + 2], 'little'):
            result[rom[offset:offset + 2].decode()] = int.from_bytes(rom[offset + 2:offset + 4], 'little')
            offset += 4
        return result

    functions = table(int.from_bytes(rom[0x14:0x16], 'little'))
    data = table(int.from_bytes(rom[0x16:0x18], 'little'))
    entries = {'lookup': int.from_bytes(rom[0x18:0x1a], 'little'), **functions}
    for name in ('SF', 'SD'):
        for index, address in enumerate(struct.unpack_from('<32I', rom, data[name])):
            entries[f'{name}_{index}'] = address
    result = {'rom_sha256': rom_hash, 'rom_version': rom[0x13],
              'disassembly_sha256': hashlib.sha256(disassembly.encode()).hexdigest(),
              'guarded_contracts': {'float_high_register_save': '0x349e: +16', 'float_high_register_restore': '0x34aa: -16',
                                    'float_multiply_lr_scratch': [hex(address) for address in sorted(scratch_lr)],
                                    'memory_byte_tail_targets': {hex(k): list(v) for k, v in branches.items()}} if branches else {},
              'scope': 'direct stack paths, excludes interrupt costs; unresolved entries do not pass', 'entries': {}}
    for name, address in entries.items():
        try:
            maximum, returns = function(address & ~1)
            row = {'address': address & ~1, 'closed': True, 'maximum_bytes': maximum}
        except ValueError as error:
            row = {'address': address & ~1, 'closed': False, 'error': str(error)}
        result['entries'][name] = row
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', required=True, type=Path)
    parser.add_argument('--disassembly', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.write_text(json.dumps(audit(args.rom.read_bytes(), args.disassembly.read_text()), indent=2) + '\n')
