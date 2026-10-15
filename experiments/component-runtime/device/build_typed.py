#!/usr/bin/env python3
"""Build the shared typed bytecode for the ESP32-C3 device experiment."""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
DEFAULT_OUTPUT = ROOT / "build/component-runtime/device/typed-artifacts"
SPEC = importlib.util.spec_from_file_location("typed_assembler", HERE.parent / "typed/assembler.py")
ASSEMBLER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ASSEMBLER)


def package(version):
    raw = bytearray(ASSEMBLER.effect_package())
    if version == 2:
        count = struct.unpack_from("<I", raw, 8)[0]
        struct.pack_into("<I", raw, 8, count + 5)
        struct.pack_into("<I", raw, 12, count)
        # Supplied seed XOR 0x13579bdf, identical to native/WAMR v2 semantics.
        instructions = (("const", 3, 0, 0, 0x13579BDF),
                        ("xor", 0, 0, 3, 0),
                        ("state_store", 0, 0, 0, 0),
                        ("state_store", 2, 0, 0, 1),
                        ("halt", 0, 0, 0, 0))
        for name, a, b, c, immediate in instructions:
            raw += struct.pack("<BBBBI", ASSEMBLER.OP[name], a, b, c, immediate)
    return bytes(raw)


def reference(seed, frame):
    x = (seed ^ (frame * 0x9E3779B9)) & 0xFFFFFFFF
    output = bytearray()
    for index in range(200):
        x = (x ^ (x << 13)) & 0xFFFFFFFF
        x ^= x >> 17
        x = (x ^ (x << 5)) & 0xFFFFFFFF
        x = (x + index + 0x6D2B79F5) & 0xFFFFFFFF
        output += bytes((x & 255, (x >> 8) & 255, (x >> 16) & 255))
    return bytes(output)


def test_host(output):
    """Exercise the exact device adapter without requiring a serial connection."""
    library = output / ("backend_typed.dylib" if sys.platform == "darwin" else "backend_typed.so")
    command = shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic", "-fPIC",
        "-dynamiclib" if sys.platform == "darwin" else "-shared",
        str(HERE / "backend_typed.c"), "-o", str(library)]
    subprocess.run(command, check=True)
    lib = ctypes.CDLL(str(library))
    lib.cr_runtime_init.restype = ctypes.c_bool
    assert lib.cr_runtime_init()
    pointer = ctypes.c_void_p
    lib.cr_load.argtypes = [pointer, ctypes.c_size_t, pointer, ctypes.c_size_t]
    lib.cr_load.restype = pointer
    lib.cr_create.argtypes = [pointer, ctypes.c_uint32, ctypes.c_uint32]
    lib.cr_create.restype = pointer
    for name in ("cr_prepare", "cr_step"):
        getattr(lib, name).argtypes = [pointer]
        getattr(lib, name).restype = ctypes.c_int
    lib.cr_begin.argtypes = [pointer, ctypes.c_uint32]
    lib.cr_begin.restype = ctypes.c_int
    lib.cr_pixels.argtypes = [pointer]
    lib.cr_pixels.restype = pointer
    lib.cr_seal.argtypes = [pointer, ctypes.c_bool]
    lib.cr_seal.restype = None
    for name in ("cr_destroy", "cr_unload"):
        getattr(lib, name).argtypes = [pointer]
        getattr(lib, name).restype = None
    error = ctypes.create_string_buffer(128)
    total_yields = 0
    for version in (1, 2):
        raw = ctypes.create_string_buffer(package(version))
        module = lib.cr_load(raw, len(package(version)), error, len(error))
        assert module, error.value
        instances = [lib.cr_create(module, seed, 200) for seed in (0x12345678, 0x87654321)]
        assert all(instances)
        for instance in instances:
            for attempt in range(2000):
                status = lib.cr_prepare(instance)
                assert status >= 0
                if status == 1:
                    break
            else:
                raise AssertionError("initializer did not terminate")
        for frame in range(3):
            for instance in instances:
                assert lib.cr_begin(instance, frame) == 0
            active = set(range(2))
            while active:
                for index in list(active):
                    status = lib.cr_step(instances[index])
                    assert status >= 0
                    if status == 1:
                        active.remove(index)
                    else:
                        total_yields += 1
            for instance, seed in zip(instances, (0x12345678, 0x87654321)):
                if version == 2:
                    seed ^= 0x13579BDF
                expected = reference(seed, frame)
                address = lib.cr_pixels(instance)
                assert ctypes.string_at(address, 600) == expected
                lib.cr_seal(instance, True)
                assert lib.cr_begin(instance, frame + 1) == -1
                assert lib.cr_step(instance) == -1
                assert lib.cr_pixels(instance) == address
                assert ctypes.string_at(address, 600) == expected
                lib.cr_seal(instance, False)
        for instance in instances:
            lib.cr_destroy(instance)
        lib.cr_unload(module)
    assert total_yields > 10
    malformed = bytearray(package(1))
    malformed[32] = 255
    raw = ctypes.create_string_buffer(bytes(malformed))
    module = lib.cr_load(raw, len(malformed), error, len(error))
    assert module  # Header parse is separate from bounded code validation.
    instance = lib.cr_create(module, 1, 200)
    assert instance and lib.cr_prepare(instance) == -1
    lib.cr_destroy(instance)
    lib.cr_unload(module)

    def tiny_artifact(instruction):
        header = struct.pack("<4s7I", b"TVM1", 1, 2, 0, 1, 8, 8, 1)
        return header + struct.pack("<BBBBI", ASSEMBLER.OP["halt"], 0, 0, 0, 0) + struct.pack("<BBBBI", *instruction)

    # Faults execute through the device API, not a second VM implementation.
    for instruction, seed, minimum_yields in (
        ((ASSEMBLER.OP["store8"], 0, 1, 0, 0), 600, 0),
        ((ASSEMBLER.OP["store8"], 0, 1, 0, 1), 0xFFFFFFFF, 0),
        ((ASSEMBLER.OP["jmp"], 0, 0, 0, 1), 1, 1000),
        ((ASSEMBLER.OP["call"], 0, 0, 0, 1), 1, 0),
    ):
        artifact = tiny_artifact(instruction)
        raw = ctypes.create_string_buffer(artifact)
        module = lib.cr_load(raw, len(artifact), error, len(error))
        assert module
        instance = lib.cr_create(module, seed, 200)
        assert instance
        while lib.cr_prepare(instance) == 0:
            pass
        assert lib.cr_begin(instance, 0) == 0
        yielded = 0
        for attempt in range(2000):
            status = lib.cr_step(instance)
            assert status != 1
            if status == -1:
                break
            yielded += 1
        else:
            raise AssertionError("fault scenario exceeded its instruction budget")
        assert yielded >= minimum_yields
        lib.cr_destroy(instance)
        lib.cr_unload(module)

    # A sealed instance remains readable even if teardown is requested early;
    # completion/unseal is the point at which its deferred destruction runs.
    raw = ctypes.create_string_buffer(package(1))
    module = lib.cr_load(raw, len(package(1)), error, len(error))
    instance = lib.cr_create(module, 0x12345678, 200)
    assert module and instance
    while lib.cr_prepare(instance) == 0:
        pass
    assert lib.cr_begin(instance, 0) == 0
    while lib.cr_step(instance) == 0:
        pass
    address = lib.cr_pixels(instance)
    expected = ctypes.string_at(address, 600)
    lib.cr_seal(instance, True)
    lib.cr_destroy(instance)
    assert ctypes.string_at(address, 600) == expected
    assert lib.cr_begin(instance, 1) == -1
    lib.cr_seal(instance, False)
    lib.cr_unload(module)
    print(f"Device typed adapter host checks passed ({total_yields} resumable yields).")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--test", action="store_true", help="compile and test the exact device adapter on the host")
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    artifacts = {}
    for version in (1, 2):
        path = args.output_dir / f"v{version}.tvm"
        path.write_bytes(package(version))
        artifacts[f"v{version}"] = {"path": str(path), "bytes": path.stat().st_size,
                                   "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    (args.output_dir / "manifest.json").write_text(json.dumps({"backend": "typed-interpreter",
        "pixels": 200, "v2_seed_xor": "13579bdf", "artifacts": artifacts}, indent=2) + "\n")
    if args.test:
        test_host(args.output_dir)
    print(json.dumps(artifacts, indent=2))


if __name__ == "__main__":
    main()
