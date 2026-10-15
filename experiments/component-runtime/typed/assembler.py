"""Assembler for the deliberately small, versioned typed VM experiment."""
from __future__ import annotations

import struct

OP = {name: i for i, name in enumerate((
    "halt", "const", "mov", "add", "xor", "mul", "shl", "shr", "and",
    "addi", "lt", "jnz", "jmp", "load8", "store8", "state_load",
    "state_store", "call", "ret",
))}


class Assembler:
    def __init__(self):
        self.code = []
        self.labels = {}

    def label(self, name):
        self.labels[name] = len(self.code)

    def emit(self, op, a=0, b=0, c=0, imm=0):
        self.code.append((OP[op], a, b, c, imm))

    def package(self, init="init", frame="frame"):
        body = b"".join(struct.pack("<BBBBI", op, a, b, c,
            self.labels[imm] if isinstance(imm, str) else imm)
            for op, a, b, c, imm in self.code)
        return struct.pack("<4s7I", b"TVM1", 1, len(self.code),
            self.labels[init], self.labels[frame], 8, 8, 1) + body


def effect_package():
    """No pixel opcode: ordinary typed arithmetic, calls, branches and byte stores."""
    a = Assembler()
    a.label("init")
    a.emit("state_store", a=0, imm=0)  # seed supplied in r0
    a.emit("state_store", a=2, imm=1)  # pixel count supplied in r2
    a.emit("halt")
    a.label("frame")
    a.emit("state_load", a=0, imm=0)
    a.emit("state_load", a=2, imm=1)
    a.emit("const", a=3, imm=0x9E3779B9)
    a.emit("mul", a=3, b=1, c=3)
    a.emit("xor", a=6, b=0, c=3)
    a.emit("const", a=4, imm=0)  # i
    a.emit("const", a=5, imm=3)
    a.label("loop")
    a.emit("lt", a=8, b=4, c=2)
    a.emit("jnz", a=8, imm="pixel")
    a.emit("halt")
    a.label("pixel")
    a.emit("call", imm="random")
    a.emit("add", a=6, b=6, c=4)
    a.emit("addi", a=6, b=6, imm=0x6D2B79F5)
    a.emit("mul", a=9, b=4, c=5)  # offset = i * 3
    a.emit("store8", a=9, b=6, c=0)
    a.emit("shr", a=7, b=6, imm=8)
    a.emit("store8", a=9, b=7, c=0, imm=1)
    a.emit("shr", a=7, b=6, imm=16)
    a.emit("store8", a=9, b=7, c=0, imm=2)
    a.emit("addi", a=4, b=4, imm=1)
    a.emit("jmp", imm="loop")
    a.label("random")
    a.emit("shl", a=7, b=6, imm=13)
    a.emit("xor", a=6, b=6, c=7)
    a.emit("shr", a=7, b=6, imm=17)
    a.emit("xor", a=6, b=6, c=7)
    a.emit("shl", a=7, b=6, imm=5)
    a.emit("xor", a=6, b=6, c=7)
    a.emit("ret")
    return a.package()


def replacement_package():
    """Different initialization code establishes a new seed, independently of r0."""
    raw = bytearray(effect_package())
    count = struct.unpack_from("<I", raw, 8)[0]
    struct.pack_into("<I", raw, 8, count + 4)
    struct.pack_into("<I", raw, 12, count)
    for instruction in ((OP["const"], 0, 0, 0, 0x11223344),
                        (OP["state_store"], 0, 0, 0, 0),
                        (OP["state_store"], 2, 0, 0, 1),
                        (OP["halt"], 0, 0, 0, 0)):
        raw += struct.pack("<BBBBI", *instruction)
    return bytes(raw)


if __name__ == "__main__":
    import argparse
    from pathlib import Path
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(effect_package())
