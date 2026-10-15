#!/usr/bin/env python3
"""Build one freestanding service artifact with Zig 0.13.0 (LLVM 18)."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import shutil

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "experiments/mqtt"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--zig", default="zig")
parser.add_argument("--output", type=Path, default=ROOT / "build/mqtt/service.wasm")
parser.add_argument("--bulk-memory", action="store_true", help="Experimental standard WASM memory.copy/fill variant")
args = parser.parse_args()
provenance = json.loads((SOURCE / "vendor/coreMQTT/SOURCE.json").read_text())
for name, digest in provenance["files"].items():
    if hashlib.sha256((SOURCE / "vendor/coreMQTT" / name).read_bytes()).hexdigest() != digest:
        raise SystemExit(f"Pinned coreMQTT source changed: {name}")
version = subprocess.check_output([args.zig, "version"], text=True).strip()
if version != "0.13.0":
    raise SystemExit(f"Expected Zig 0.13.0, found {version}")
args.output.parent.mkdir(parents=True, exist_ok=True)
command = [args.zig, "cc", "-target", "wasm32-freestanding", "-Oz", "-flto", "-fno-builtin", "-nostdlib",
           "-DNDEBUG", "-DMQTT_DO_NOT_USE_CUSTOM_CONFIG", "-I" + str(SOURCE / "freestanding"),
           "-I" + str(SOURCE / "vendor/coreMQTT/include"), str(SOURCE / "service.c"),
           str(SOURCE / "libc.c"), str(SOURCE / "vendor/coreMQTT/core_mqtt_serializer.c"),
           "-Wl,--no-entry", "-Wl,--initial-memory=0x10000", "-Wl,--max-memory=0x10000",
           "-Wl,-z,stack-size=0x1000", "-Wl,--strip-all", "-o", str(args.output)]
if args.bulk_memory:
    command[2:2] = ["-mbulk-memory", "-DLIGHTVM_BULK_MEMORY=1"]
subprocess.run(command, check=True, env=__import__("os").environ | {"ZIG_GLOBAL_CACHE_DIR": str(ROOT / "build/mqtt/zig-cache")})
data = args.output.read_bytes()
report = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "zig": version,
          "bulk_memory": args.bulk_memory,
          "serializer": "coreMQTT v2.3.1", "linear_memory_bytes": 65536,
          "coreMQTT_archive_sha256": "b8e95044e6ef8381610949b7fe546c5ddf4e52989ce9938209d5dd5f3371b5d7"}
args.output.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
shutil.copyfile(ROOT / "LICENSE", args.output.parent / "LICENSE")
shutil.copyfile(ROOT / "licenses/coreMQTT-v2.3.1.txt", args.output.parent / "coreMQTT-LICENSE.txt")
print(json.dumps(report))
