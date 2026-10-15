#!/usr/bin/env python3
"""Build and run the host typed-VM experiment; no downloaded dependencies."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys

from assembler import effect_package, replacement_package

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / "build/component-runtime/typed"
LIMITATIONS = [
    "Host C11 prototype; no MCU build, physical LED, DMA, cache-coherency, flash-write, multicore or browser compiler validation.",
    "The peripheral is a simulated deferred consumer holding and checking the same RGB allocation. Communications use real local AF_UNIX socketpair bytes, not Wi-Fi, USB, Matter or MQTT.",
    "Safe points occur between every VM instruction; native bulk transforms, allocation, package copying and host file I/O remain synchronous. Measured maxima are observations, not worst-case timing proofs.",
    "Timing uses monotonic wall-clock durations around resume calls, excluding service work. Host scheduling interference remains possible; harness timings must not be compared as interpreter throughput.",
    "Tracked allocation figures cover program packages, instance continuations and native RGB buffers. They exclude host process memory, libc allocator headers, C stacks (including the 32,800-byte input-file staging array), socket buffers and executable text.",
    "Artifact verification restricts opcodes, register classes, branch targets, capability/version fields and fixed memory limits; runtime enforces bounds, view generations, ownership and call/job limits. This is a research implementation, not a security-audited sandbox.",
    "The program uses u32 operations and RGB8 views; four source-language frontends, dynamic objects, floating-point semantics, deadlines, state migration and persistent interrupted-install recovery are not implemented.",
    "Preparation validation is incremental, but file staging and package allocation/copy are not. No arbitrary-load or native-call preemption guarantee is claimed.",
]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(sanitize=False):
    BUILD.mkdir(parents=True, exist_ok=True)
    compiler = shlex.split(os.environ.get("CC", "cc"))
    if not compiler or not shutil.which(compiler[0]):
        raise FileNotFoundError("C compiler is unavailable; set CC to clang or gcc")
    binary = BUILD / ("typed-vm-sanitized" if sanitize else "typed-vm")
    flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    if sanitize:
        flags += ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    command = compiler + flags + [str(HERE / "vm.c"), "-o", str(binary)]
    subprocess.run(command, check=True, capture_output=True, text=True)
    package = BUILD / "effect.tvm"
    package.write_bytes(effect_package())
    (BUILD / "replacement.tvm").write_bytes(replacement_package())
    version = subprocess.run(compiler + ["--version"], check=True, capture_output=True, text=True).stdout.splitlines()[0]
    return binary, package, {"compiler": version, "command": command}


def run(output, sanitize=False):
    envelope = {
        "schema_version": 1, "backend": "typed-interpreter", "status": "failed",
        "environment": {"platform": platform.platform(), "machine": platform.machine(), "python": sys.version.split()[0]},
        "workload": {"pixels": 300, "seed": 305419896, "frame_hashes": []},
        "checks": {}, "metrics": {}, "limitations": LIMITATIONS,
    }
    exit_code = 1
    try:
        binary, package, build_info = build(sanitize)
        envelope["environment"].update(build_info)
        result = subprocess.run([str(binary), str(package)], capture_output=True, text=True, timeout=30)
        if result.stdout:
            envelope.update(json.loads(result.stdout))
        envelope["environment"]["sanitizers"] = "address,undefined" if sanitize else "none"
        envelope["artifacts"] = {
            name: {"path": str(path.relative_to(ROOT)), "bytes": path.stat().st_size, "sha256": sha256(path)}
            for name, path in (("bytecode", package), ("replacement_bytecode", BUILD / "replacement.tvm"), ("host_runtime", binary), ("runtime_source", HERE / "vm.c"))
        }
        if result.stderr:
            envelope["diagnostics"] = result.stderr
        if result.returncode != 0:
            envelope["status"] = "failed"
            envelope["error"] = f"host harness exited {result.returncode}"
        exit_code = 0 if result.returncode == 0 and envelope["status"] == "passed" else 1
    except FileNotFoundError as error:
        envelope["status"] = "unavailable"
        envelope["error"] = str(error)
    except (subprocess.SubprocessError, ValueError, OSError) as error:
        envelope["error"] = str(error)
        if isinstance(error, subprocess.CalledProcessError):
            envelope["diagnostics"] = error.stderr
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(envelope, indent=2) + "\n")
    print(f"typed-interpreter: {envelope['status']} ({output})")
    return exit_code


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=BUILD / "results.json")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    raise SystemExit(run(args.output, args.sanitize))
