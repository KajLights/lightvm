"""Source fixtures shared by protocol, persistence and native contract tests."""
from pathlib import Path
import struct
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.compiler import compile_source

PREFIX = 'from lightvm import profile,init,render,rgb\nprofile("animation")\n'


def program(source):
    result = compile_source(source)
    if not result.success:
        raise AssertionError(result.diagnostics)
    return result.bytecode


def color(red=1, green=2, blue=3):
    return program(PREFIX + f'@render\ndef draw(ctx,runtime,pixels):\n    pixels.fill(rgb({red},{green},{blue}))\n')


def sample(name):
    return program((ROOT / 'examples/animations' / (name + '.py')).read_text())


def fault():
    return program(PREFIX + '@render\ndef draw(ctx,runtime,pixels):\n    pixels[pixels.count] = rgb(1,2,3)\n')


def padded_program(size):
    data = bytearray(color())
    regs, code, palettes = struct.unpack_from('<III', data, 8)
    functions = struct.unpack_from('<I', data, 20)[0]
    manifest_size = struct.unpack_from('<I', data, 76)[0]
    body = data[:-manifest_size]
    manifest = data[-manifest_size:]
    extra = size - len(data)
    if extra < 0:
        raise ValueError('size below fixture minimum')
    instructions = min(extra // 8, 2048-code)
    # Extend the final function with unreachable END instructions.
    code_end = 96 + regs * 8 + code * 8
    body[code_end:code_end] = bytes(instructions * 8)
    struct.pack_into('<I', body, 12, code+instructions)
    extra -= instructions * 8
    colors = min(extra // 4, 1024-palettes)
    palette_end = code_end + instructions * 8 + palettes * 4
    body[palette_end:palette_end] = bytes(colors * 4)
    struct.pack_into('<I', body, 16, palettes+colors)
    extra -= colors * 4
    if manifest_size + extra > 8192:
        raise ValueError('size exceeds valid fixture capacity')
    final_function = palette_end + colors * 4 + (functions - 1) * 24
    struct.pack_into('<I', body, final_function + 4, code + instructions)
    struct.pack_into('<I', body, 76, manifest_size+extra)
    return bytes(body + manifest + b' ' * extra)
