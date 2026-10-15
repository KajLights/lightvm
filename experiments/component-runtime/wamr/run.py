#!/usr/bin/env python3
"""Build and exercise genuine WAMR AOT. Never substitutes another execution engine."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / "build/component-runtime"
DEPS = BUILD / "deps"
REVISION = "25bd7eb63e828e4bd242cc9b38d260b4b31c6605"
SOURCE = DEPS / "wamr-2.4.5"
COMMON = "independent_instances resumable_execution native_buffer_alias native_bulk_operation async_output_retention service_progress replacement failed_replacement_preserves_old bounds_rejection stale_view_rejection runaway_bounded admission_failure malformed_artifact_rejected cleanup".split()
DOWNLOADS = {
    "source": (f"https://codeload.github.com/wasm-micro-runtime/wasm-micro-runtime/tar.gz/{REVISION}", "4ea552d9d979f3c3d5f9e298a1594150820d2b76e8328ad68779578edea7a873", DEPS / "wamr-source.tar.gz"),
    "macos": ("https://github.com/wasm-micro-runtime/wasm-micro-runtime/releases/download/WAMR-2.4.5/wamrc-2.4.5-x86_64-macos-15-intel.tar.gz", "419d41b8b9b29bb8bb0e86e0409725abab81aa693daa479e502fadd0c206db69", DEPS / "wamrc-macos.tar.gz"),
    "linux": ("https://github.com/wasm-micro-runtime/wasm-micro-runtime/releases/download/WAMR-2.4.5/wamrc-2.4.5-x86_64-ubuntu-22.04.tar.gz", "fedda99950a70f7dc45470ce88e12bf9e435b9482058ae97ce2cadd760e2329f", DEPS / "wamrc-linux.tar.gz"),
    "zstd": ("https://codeload.github.com/facebook/zstd/tar.gz/refs/tags/v1.5.7", "37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3", DEPS / "wamr-zstd.tar.gz"),
}


class Unavailable(RuntimeError):
    pass


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def command(args, *, env=None, timeout=180):
    result = subprocess.run([str(x) for x in args], cwd=ROOT, env=env, capture_output=True, text=True, timeout=timeout)
    with (BUILD / "wamr-commands.log").open("a") as log:
        log.write("$ " + " ".join(map(str, args)) + "\n" + result.stdout + result.stderr + "\n")
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {args[0]}\n{result.stderr[-3000:]}\n{result.stdout[-1500:]}")
    return result.stdout.strip()


def fetch(name):
    url, expected, destination = DOWNLOADS[name]
    if not destination.exists():
        temporary = destination.with_suffix(".download")
        urllib.request.urlretrieve(url, temporary)
        temporary.replace(destination)
    if digest(destination) != expected:
        raise RuntimeError(f"Checksum mismatch: {destination}")
    return destination


def unpack(archive, destination, strip=False):
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as tar:
        if strip:
            for member in tar.getmembers():
                parts = Path(member.name).parts
                if len(parts) <= 1:
                    continue
                member.name = str(Path(*parts[1:]))
                tar.extract(member, destination, filter="data")
        else:
            tar.extractall(destination, filter="data")


def bootstrap():
    """Optional network step; dependencies stay beneath ignored build/."""
    if not (SOURCE / "core").is_dir():
        unpack(fetch("source"), SOURCE, strip=True)
    compiler = DEPS / "wamrc-2.4.5"
    if platform.system() == "Darwin":
        if not (compiler / "wamrc").exists():
            unpack(fetch("macos"), compiler)
        zstd = DEPS / "wamr-zstd"
        if not (zstd / "lib/libzstd.1.dylib").exists():
            unpack(fetch("zstd"), zstd, strip=True)
            command(["make", "-C", zstd / "lib", "-j4", "libzstd-mt", "CC=clang -arch x86_64"], timeout=300)
    elif platform.system() == "Linux" and platform.machine() == "x86_64":
        if not (compiler / "wamrc").exists():
            unpack(fetch("linux"), compiler)
    else:
        raise Unavailable("Supply WAMRC for this host: bundled bootstrap supports macOS and Linux x86_64.")
    if not shutil.which("cmake") and not (DEPS / "wamr-python/cmake/data/bin/cmake").exists():
        command([sys.executable, "-m", "pip", "install", "--target", DEPS / "wamr-python", "cmake==3.31.6"], timeout=300)


def find_tool(variable, name, candidates=()):
    choices = [os.environ.get(variable), shutil.which(name), *(str(p) for p in candidates)]
    for value in choices:
        if value and Path(value).is_file():
            return str(Path(value).resolve())
    raise Unavailable(f"Missing {name}; set {variable} to its executable path. See README.md.")


def validate_manifest(manifest, artifact, target):
    expected = {"schema_version": 1, "backend": "wamr-aot", "api_version": 1,
                "target": target, "pixels": 300, "buffer_bytes": 900,
                "max_instances": 3, "quantum_pixels": 32, "max_guest_quantum": 64,
                "runtime_heap_budget_bytes": 8 * 1024 * 1024,
                "trust": "generated-local-artifact"}
    if set(manifest) != set(expected) | {"sha256"}:
        raise ValueError("Manifest fields do not match this experiment's capability profile")
    for field, value in expected.items():
        if type(manifest[field]) is not type(value) or manifest[field] != value:
            raise ValueError(f"Manifest profile mismatch: {field}")
    if artifact.read_bytes()[:4] != b"\x00aot":
        raise ValueError("Artifact is not a WAMR AOT package")
    if manifest["sha256"] != digest(artifact):
        raise ValueError("Artifact digest mismatch")


def run(args):
    if args.bootstrap:
        bootstrap()
    if not (SOURCE / "core").is_dir():
        raise Unavailable("Pinned WAMR source missing; run with --bootstrap.")
    # Verify the downloaded pinned archive whenever present, including cached runs.
    if DOWNLOADS["source"][2].exists() and digest(DOWNLOADS["source"][2]) != DOWNLOADS["source"][1]:
        raise RuntimeError("Pinned WAMR source archive checksum mismatch")
    cmake = find_tool("CMAKE", "cmake", [DEPS / "wamr-python/cmake/data/bin/cmake"])
    clang = find_tool("WASM_CLANG", "clang-22", [Path("/opt/homebrew/opt/llvm@22/bin/clang"), Path("/opt/homebrew/opt/llvm/bin/clang"), shutil.which("clang") or ""])
    rust_linkers = sorted((Path.home() / ".rustup/toolchains").glob("*/lib/rustlib/*/bin/gcc-ld/wasm-ld"), reverse=True)
    linker = find_tool("WASM_LD", "wasm-ld", [Path(clang).parent / "wasm-ld", *rust_linkers])
    wamrc = find_tool("WAMRC", "wamrc", [DEPS / "wamrc-2.4.5/wamrc"])
    env = os.environ.copy()
    if platform.system() == "Darwin":
        env["DYLD_LIBRARY_PATH"] = str(DEPS / "wamr-zstd/lib") + (":" + env["DYLD_LIBRARY_PATH"] if env.get("DYLD_LIBRARY_PATH") else "")
    compiler_version = command([wamrc, "--version"], env=env)
    if compiler_version != "wamrc 2.4.5":
        raise Unavailable(f"This fixture requires wamrc 2.4.5, got: {compiler_version}")
    target = {"arm64": "aarch64", "aarch64": "aarch64", "x86_64": "x86_64"}.get(platform.machine())
    if target is None:
        raise Unavailable(f"Unsupported host: {platform.machine()}")
    obj, wasm, aot = [BUILD / f"wamr-guest.{ext}" for ext in ("o", "wasm", "aot")]
    command([clang, "--target=wasm32", "-O2", "-nostdlib", "-fno-builtin", "-mno-simd128", "-c", HERE / "guest.c", "-o", obj])
    command([linker, "--no-entry", "--export-all", "--initial-memory=65536", "--max-memory=65536", "-z", "stack-size=4096", obj, "-o", wasm])
    command([wamrc, f"--target={target}", "--bounds-checks=1", "--stack-bounds-checks=1", "--disable-simd", "-o", aot, wasm], env=env)
    replacement_obj, replacement_wasm, replacement_aot = [BUILD / f"wamr-replacement.{ext}" for ext in ("o", "wasm", "aot")]
    command([clang, "--target=wasm32", "-O2", "-nostdlib", "-fno-builtin", "-mno-simd128", "-DCOMPONENT_SEED_XOR=0x5a5a5a5a", "-c", HERE / "guest.c", "-o", replacement_obj])
    command([linker, "--no-entry", "--export-all", "--initial-memory=65536", "--max-memory=65536", "-z", "stack-size=4096", replacement_obj, "-o", replacement_wasm])
    command([wamrc, f"--target={target}", "--bounds-checks=1", "--stack-bounds-checks=1", "--disable-simd", "-o", replacement_aot, replacement_wasm], env=env)
    manifest = dict(schema_version=1, backend="wamr-aot", api_version=1, target=target,
                    pixels=300, buffer_bytes=900, max_instances=3, quantum_pixels=32,
                    max_guest_quantum=64, runtime_heap_budget_bytes=8*1024*1024,
                    trust="generated-local-artifact", sha256=digest(aot))
    manifest_path = BUILD / "wamr-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    validate_manifest(json.loads(manifest_path.read_text()), aot, target)
    host_build = BUILD / ("wamr-host-sanitized" if args.sanitize else "wamr-host")
    options = [cmake, "-S", HERE, "-B", host_build, f"-DWAMR_SOURCE={SOURCE}", "-DCMAKE_BUILD_TYPE=Release"]
    if args.sanitize:
        options += ["-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer", "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined"]
    command(options)
    command([cmake, "--build", host_build, "-j4"], timeout=300)
    replacement_manifest = dict(manifest, sha256=digest(replacement_aot))
    replacement_manifest_path = BUILD / "wamr-replacement-manifest.json"
    replacement_manifest_path.write_text(json.dumps(replacement_manifest, indent=2) + "\n")
    validate_manifest(replacement_manifest, replacement_aot, target)
    output = command([host_build / "wamr_probe", aot, replacement_aot], timeout=30)
    report = json.loads(next(line for line in reversed(output.splitlines()) if line.startswith("{")))
    if any(report["checks"].get(name) is not True for name in COMMON):
        raise RuntimeError("Host returned incomplete or failed common checks")
    report["environment"].update(system=platform.platform(), architecture=platform.machine(),
                                 wamr_revision=REVISION, wamrc=compiler_version,
                                 wasm_clang=command([clang, "--version"]).splitlines()[0],
                                 wasm_linker=command([linker, "--version"]),
                                 native_compiler=command([os.environ.get("CC", "cc"), "--version"]).splitlines()[0],
                                 sanitizer=args.sanitize)
    report["artifacts"] = {path.name: {"sha256": digest(path), "bytes": path.stat().st_size}
                           for path in [wasm, aot, replacement_wasm, replacement_aot, manifest_path, replacement_manifest_path, host_build / "wamr_probe"]}
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--bootstrap", action="store_true", help="Download verified pinned dependencies locally")
    parser.add_argument("--sanitize", action="store_true", help="Build host and runtime with ASan/UBSan; AOT guest remains bounds-checked")
    args = parser.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True); DEPS.mkdir(exist_ok=True)
    try:
        report = run(args)
        result = 0
    except (RuntimeError, OSError, subprocess.SubprocessError, ValueError) as exc:
        report = dict(schema_version=1, backend="wamr-aot", status="unavailable" if isinstance(exc, Unavailable) else "failed",
                      environment={"system": platform.platform()}, workload={"pixels": 300, "seed": 305419896, "frame_hashes": []},
                      checks={name: False for name in COMMON}, metrics={}, limitations=[str(exc)])
        print(str(exc), file=sys.stderr); result = 2 if isinstance(exc, Unavailable) else 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"wamr-aot: {report['status']}; {args.output}")
    return result


if __name__ == "__main__":
    raise SystemExit(main())
