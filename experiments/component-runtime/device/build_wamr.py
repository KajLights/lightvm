#!/usr/bin/env python3
"""Produce pinned WAMR RV32IMC ILP32 packages; reuse cached tools, no installs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
BUILD=ROOT/"build/component-runtime"
DEPS=BUILD/"deps"
OUT=BUILD/"device/wamr-artifacts"

def tool(variable,candidates):
    for value in [os.environ.get(variable),*map(str,candidates)]:
        if value and Path(value).is_file(): return str(Path(value).absolute())
    raise RuntimeError(f"Missing cached tool {variable}; this script does not install dependencies")

def run(command,env):
    completed=subprocess.run(list(map(str,command)),env=env,text=True,capture_output=True,check=True,timeout=120)
    print(completed.stdout,end="")
    return completed.stdout.strip()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir",type=Path,default=OUT)
    args=parser.parse_args(); out=args.output_dir; out.mkdir(parents=True,exist_ok=True)
    clang=tool("WASM_CLANG",["/opt/homebrew/opt/llvm@22/bin/clang",shutil.which("clang") or ""])
    linkers=sorted((Path.home()/".rustup/toolchains").glob("*/lib/rustlib/*/bin/gcc-ld/wasm-ld"),reverse=True)
    ld=tool("WASM_LD",[shutil.which("wasm-ld") or "",*linkers])
    wamrc=tool("WAMRC",[DEPS/"wamrc-2.4.5/wamrc",shutil.which("wamrc") or ""])
    env=os.environ.copy()
    if platform.system()=="Darwin": env["DYLD_LIBRARY_PATH"]=str(DEPS/"wamr-zstd/lib")
    version=run([wamrc,"--version"],env)
    if version!="wamrc 2.4.5": raise RuntimeError(f"Expected pinned wamrc 2.4.5, got {version}")
    metadata={"backend":"wamr-aot","wamr_revision":"25bd7eb63e828e4bd242cc9b38d260b4b31c6605","compiler":version,
              "target":"riscv32","abi":"ilp32","cpu_features":"+m,+c,-a,-f,-d",
              "pixels":200,"buffer_bytes_per_instance":600,"quantum_pixels":32,"replacement_seed_xor":"13579bdf","artifacts":{}}
    for version,xor in [(1,0),(2,0x13579bdf)]:
        prefix=out/f"v{version}"
        run([clang,"--target=wasm32","-O2","-nostdlib","-fno-builtin","-mno-simd128",f"-DCR_SEED_XOR={xor}u","-c",HERE/"components/wamr/guest.c","-o",prefix.with_suffix(".o")],env)
        run([ld,"--no-entry","--export-all","--initial-memory=65536","--max-memory=65536","-z","stack-size=2048",prefix.with_suffix(".o"),"-o",prefix.with_suffix(".wasm")],env)
        run([wamrc,"--target=riscv32","--target-abi=ilp32","--cpu=generic-rv32","--cpu-features=+m,+c,-a,-f,-d","--bounds-checks=1","--stack-bounds-checks=1","--disable-simd","--disable-ref-types","--disable-llvm-jump-tables","-o",prefix.with_suffix(".aot"),prefix.with_suffix(".wasm")],env)
        artifact=prefix.with_suffix(".aot"); content=artifact.read_bytes()
        if content[:4]!=b"\x00aot": raise RuntimeError("Compiler did not produce WAMR AOT")
        section,section_size=struct.unpack_from("<II",content,8)
        binary,abi,elftype,machine,elfversion,flags=struct.unpack_from("<HHHHII",content,16)
        arch=content[48:64].split(b"\0")[0].decode()
        if section!=0 or section_size!=48 or binary!=0 or machine!=243 or flags&6 or arch!="riscv32":
            raise RuntimeError("AOT target metadata is not little-endian RV32 soft-float")
        metadata["artifacts"][artifact.name]={"sha256":hashlib.sha256(content).hexdigest(),"bytes":len(content),
                                               "aot_machine":machine,"aot_elf_flags":flags,"aot_arch":arch}
    if metadata["artifacts"]["v1.aot"]["sha256"]==metadata["artifacts"]["v2.aot"]["sha256"]: raise RuntimeError("Replacement executable must differ")
    (out/"manifest.json").write_text(json.dumps(metadata,indent=2)+"\n")
    print(out/"manifest.json")

if __name__=="__main__": main()
