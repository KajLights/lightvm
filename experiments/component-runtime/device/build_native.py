#!/usr/bin/env python3
"""Build import-free RV32IMC/ILP32 packages for the ESP32-C3 experiment."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
DEFAULT_TOOLS = Path("/tmp/lightvm-esp-deps/tools/tools/riscv32-esp-elf/esp-14.2.0_20251107/riscv32-esp-elf/bin")
DEFAULT_OUTPUT = ROOT / "build/component-runtime/device/native-artifacts"
MAGIC, ABI, TARGET, HEADER_BYTES = 0x314E5243, 1, 0x00C30001, 64


def build(output, toolchain):
    output.mkdir(parents=True, exist_ok=True)
    tools = {}
    for name in ("gcc", "objcopy", "objdump", "readelf", "nm"):
        path = toolchain / ("riscv32-esp-elf-" + name)
        found = str(path) if path.is_file() else shutil.which("riscv32-esp-elf-" + name)
        if not found:
            raise RuntimeError(f"Missing RISC-V tool: {name}")
        tools[name] = found
    commands = []

    def command(args):
        commands.append(args)
        result = subprocess.run(args, check=True, capture_output=True, text=True)
        return result.stdout

    compiler = command([tools["gcc"], "--version"]).splitlines()[0]
    packages = []
    common = ["-march=rv32imc", "-mabi=ilp32", "-mcmodel=medany", "-mno-relax",
              "-msmall-data-limit=0", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
              "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie", "-fno-common",
              "-fno-jump-tables", "-fno-tree-loop-distribute-patterns", "-fstack-usage",
              "-fno-stack-protector", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
              "-ffunction-sections", "-fdata-sections", "-fvisibility=hidden"]
    for revision in (1, 2):
        stem = output / f"native-v{revision}"
        obj, elf, binary = (stem.with_suffix(ext) for ext in (".o", ".elf", ".bin"))
        command([tools["gcc"], *common, f"-DNATIVE_REVISION={revision}", "-c",
                 str(HERE / "native_entry.c"), "-o", str(obj)])
        undefined_object = command([tools["nm"], "-u", str(obj)]).strip()
        if undefined_object:
            raise RuntimeError(f"Package has external imports: {undefined_object}")
        command([tools["gcc"], "-march=rv32imc", "-mabi=ilp32", "-mcmodel=medany",
                 "-mno-relax", "-nostdlib", "-nostartfiles", "-static", "-no-pie",
                 "-Wl,--no-relax,--gc-sections,--build-id=none",
                 "-Wl,-T," + str(HERE / "native.ld"), str(obj), "-o", str(elf)])
        relocations = command([tools["readelf"], "-rW", str(elf)])
        if "There are no relocations in this file" not in relocations:
            raise RuntimeError("Final package ELF retains relocations: " + relocations)
        if command([tools["nm"], "-u", str(elf)]).strip():
            raise RuntimeError("Final package ELF has undefined symbols")
        symbols = command([tools["nm"], "-n", str(elf)])
        if not re.search(r"^0+ T native_entry$", symbols, re.MULTILINE):
            raise RuntimeError("Payload entry offset is not zero")
        header_info = command([tools["readelf"], "-hAW", str(elf)])
        if "ELF32" not in header_info or "RISC-V" not in header_info:
            raise RuntimeError("Incorrect target ELF class or ISA")
        disassembly = command([tools["objdump"], "-drw", str(elf)])
        stem.with_suffix(".disassembly.txt").write_text(disassembly)
        stem.with_suffix(".elf-info.txt").write_text(header_info + relocations + symbols)
        # With medany + --no-relax, symbol/data relocations are PC-relative.
        # Inspect input relocations too: absolute address relocations invalidate
        # the relocate-by-copy model even when the final ELF resolves them.
        object_relocations = command([tools["readelf"], "-rW", str(obj)])
        relocation_types = set(re.findall(r"\bR_RISCV_[A-Z0-9_]+\b", object_relocations))
        allowed = {"R_RISCV_CALL_PLT", "R_RISCV_PCREL_HI20", "R_RISCV_PCREL_LO12_I",
                   "R_RISCV_PCREL_LO12_S", "R_RISCV_BRANCH", "R_RISCV_JAL",
                   "R_RISCV_RVC_BRANCH", "R_RISCV_RVC_JUMP"}
        if relocation_types - allowed:
            raise RuntimeError(f"Non-relative package relocations: {relocation_types - allowed}")
        stack = {}
        for line in stem.with_suffix(".su").read_text().splitlines():
            location, amount, kind = line.split("\t")
            if kind != "static":
                raise RuntimeError(f"Dynamic stack use: {line}")
            stack[location.split(":")[-1]] = int(amount)
        # Entry can call at most one component leaf. Sum is conservative even
        # when optimization inlines the leaf functions into the dispatcher.
        stack_bound = stack.get("native_entry", 0) + max(
            (size for name, size in stack.items() if name != "native_entry"), default=0)
        if stack_bound > 512:
            raise RuntimeError(f"Component static stack exceeds policy: {stack_bound}")
        command([tools["objcopy"], "-O", "binary", "-j", ".text", "-j", ".rodata",
                 str(elf), str(binary)])
        payload = binary.read_bytes()
        payload += b"\0" * ((-len(payload)) % 4)
        if not payload or len(payload) > 16384:
            raise RuntimeError("Native payload size outside experiment limit")
        crc = zlib.crc32(payload) & 0xffffffff
        header = struct.pack("<16I", MAGIC, ABI, TARGET, HEADER_BYTES, len(payload),
                             0, crc, revision, 40, 32, 512, 1, 0, 0, 0, 0)
        package = stem.with_suffix(".crn")
        package.write_bytes(header + payload)
        packages.append({
            "path": str(package), "revision": revision, "payload_bytes": len(payload),
            "package_bytes": package.stat().st_size,
            "sha256": hashlib.sha256(package.read_bytes()).hexdigest(),
            "payload_crc32": f"{crc:08x}", "state_bytes": 40,
            "stack_frames_bytes": stack, "conservative_call_stack_bytes": stack_bound,
            "input_relocation_types": sorted(relocation_types),
            "undefined_symbols": [], "final_relocations": [],
        })
    report = {"schema_version": 1, "backend": "direct-native",
              "target": "ESP32-C3 RV32IMC ILP32", "compiler": compiler,
              "commands": commands, "packages": packages,
              "trust": "audited producer; CRC/ABI do not sandbox arbitrary native code",
              "loader_requirements": ["CONFIG_ESP_SYSTEM_MEMPROT_FEATURE=n",
                                      "MALLOC_CAP_EXEC | MALLOC_CAP_32BIT", "fence.i"]}
    (output / "build-info.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, default=DEFAULT_TOOLS)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    report = build(args.output_dir, args.toolchain)
    print(json.dumps({"target": report["target"], "packages": report["packages"]}, indent=2))


if __name__ == "__main__":
    main()
