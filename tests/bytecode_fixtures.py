"""Source fixtures shared by protocol, persistence and native contract tests."""
from pathlib import Path
import struct
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.compiler import compile_source


def program(source):
    result = compile_source(source)
    if not result.success:
        raise AssertionError(result.diagnostics)
    return result.bytecode


def color(red=1, green=2, blue=3):
    return program(f'from lightvm import rgb, kernel\n@kernel\ndef draw():\n    pixels.fill(rgb({red},{green},{blue}))\n')


def sample(name):
    return program((ROOT / 'samples/python' / (name + '.py')).read_text())


def fault():
    return program('from lightvm import rgb, kernel\n@kernel\ndef draw():\n    pixels[count] = rgb(1,2,3)\n')


def infinite():
    data = bytearray(color())
    code = 64 + struct.unpack_from('<I', data, 8)[0] * 8
    entry = struct.unpack_from('<I', data, 28)[0]
    # An otherwise valid kernel whose first instruction branches to itself.
    struct.pack_into('<BBBBI', data, code + entry * 8, 19, 0, 0, 0, entry)
    return bytes(data)


def padded_program(size):
    data = bytearray(color())
    regs, code, palettes = struct.unpack_from('<III', data, 8)
    manifest_size = struct.unpack_from('<I', data, 36)[0]
    body = data[:-manifest_size]
    manifest = data[-manifest_size:]
    extra = size - len(data)
    if extra < 0:
        raise ValueError('size below fixture minimum')
    instructions = min(extra // 8, 2048-code)
    # Unreachable END instructions before the palette remain valid kernel code.
    body += bytes(instructions * 8)
    struct.pack_into('<I', body, 12, code+instructions)
    extra -= instructions * 8
    colors = min(extra // 4, 1024-palettes)
    body += bytes(colors * 4)
    struct.pack_into('<I', body, 16, palettes+colors)
    extra -= colors * 4
    if manifest_size + extra > 8192:
        raise ValueError('size exceeds valid fixture capacity')
    struct.pack_into('<I', body, 36, manifest_size+extra)
    return bytes(body + manifest + b' ' * extra)
