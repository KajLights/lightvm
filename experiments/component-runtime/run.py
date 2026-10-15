#!/usr/bin/env python3
"""Build/run component prototypes and independently verify their common results.

This coordinator deliberately does not rank host timings: each experiment reports
its own execution boundaries and memory-accounting exclusions.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BACKENDS = {
    "wamr": "wamr-aot",
    "native": "direct-native",
    "typed": "typed-interpreter",
}
COMMON_CHECKS = (
    "independent_instances", "resumable_execution", "native_buffer_alias",
    "native_bulk_operation", "async_output_retention", "service_progress",
    "replacement", "failed_replacement_preserves_old", "bounds_rejection",
    "stale_view_rejection", "runaway_bounded", "admission_failure",
    "malformed_artifact_rejected", "cleanup",
)
PIXELS = 300
SEED = 0x12345678
MASK = 0xFFFFFFFF


def reference_frame(pixels: int, seed: int, frame: int) -> bytes:
    """Independent byte-for-byte oracle; never imports a backend implementation."""
    x = (seed ^ ((frame * 0x9E3779B9) & MASK)) & MASK
    rgb = bytearray()
    for i in range(pixels):
        x = (x ^ (x << 13)) & MASK
        x = (x ^ (x >> 17)) & MASK
        x = (x ^ (x << 5)) & MASK
        x = (x + i + 0x6D2B79F5) & MASK
        rgb.extend((x & 255, (x >> 8) & 255, (x >> 16) & 255))
    return bytes(rgb)


def fnv1a(data: bytes) -> str:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & MASK
    return f"{value:08x}"


def expected_workload() -> dict:
    return {
        "pixels": PIXELS,
        "seed": SEED,
        "frame_hashes": [fnv1a(reference_frame(PIXELS, SEED, n)) for n in range(3)],
    }


def validate_result(result: object, backend: str) -> list[str]:
    """A successful process is insufficient: missing evidence is a failure."""
    if not isinstance(result, dict):
        return ["result must be a JSON object"]
    errors = []
    if type(result.get("schema_version")) is not int or result["schema_version"] != 1:
        errors.append("schema_version must be integer 1")
    if result.get("backend") != BACKENDS[backend]:
        errors.append(f"backend must be {BACKENDS[backend]}")
    if result.get("status") != "passed":
        errors.append(f"backend status is {result.get('status')!r}, not passed")
    for field in ("environment", "metrics"):
        if not isinstance(result.get(field), dict):
            errors.append(f"{field} must be an object")
    limitations = result.get("limitations")
    if not isinstance(limitations, list) or any(not isinstance(v, str) for v in limitations):
        errors.append("limitations must be an array of strings")
    workload = result.get("workload")
    if not isinstance(workload, dict):
        errors.append("workload must be an object")
    else:
        for key, expected in expected_workload().items():
            if workload.get(key) != expected:
                errors.append(f"workload.{key}: expected {expected!r}, got {workload.get(key)!r}")
    checks = result.get("checks")
    if not isinstance(checks, dict):
        errors.append("checks must be an object")
    else:
        for name in COMMON_CHECKS:
            if checks.get(name) is not True:
                errors.append(f"check {name}: expected true, got {checks.get(name)!r}")
        for name, passed in checks.items():
            if type(passed) is not bool:
                errors.append(f"check {name} is not a boolean")
            elif not passed and name not in COMMON_CHECKS:
                errors.append(f"additional check {name} failed")
    return errors


def execute_backend(name: str, run_dir: Path, timeout: float) -> dict:
    result_path = run_dir / f"{name}.json"
    log_path = run_dir / f"{name}.log"
    command = [sys.executable, str(HERE / name / "run.py"), "--output", str(result_path)]
    record = {"command": command, "log": str(log_path), "result_path": str(result_path)}
    start = time.monotonic()
    try:
        with log_path.open("w") as log:
            process = subprocess.Popen(
                command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            try:
                code = process.wait(timeout=timeout)
            except (subprocess.TimeoutExpired, KeyboardInterrupt) as error:
                # Kill compiler grandchildren as well as the Python runner.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
                if isinstance(error, KeyboardInterrupt):
                    raise
                record.update(status="failed", errors=[f"timed out after {timeout:g} seconds"])
                return record
        record["returncode"] = code
        try:
            result = json.loads(result_path.read_text())
        except (OSError, ValueError) as error:
            record.update(status="failed", errors=[f"no readable result: {error}"])
            return record
        record["result"] = result
        errors = validate_result(result, name)
        if code:
            errors.insert(0, f"runner exited with status {code}")
        record["errors"] = errors
        if isinstance(result, dict) and result.get("status") == "unavailable":
            record["status"] = "unavailable"
        else:
            record["status"] = "failed" if errors else "passed"
        return record
    except OSError as error:
        record.update(status="failed", errors=[str(error)])
        return record
    finally:
        record["runner_wall_seconds"] = round(time.monotonic() - start, 6)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", action="append", choices=BACKENDS,
                        help="select backends; default runs all three")
    parser.add_argument("--output", type=Path, default=ROOT / "build/component-runtime/comparison.json")
    parser.add_argument("--timeout", type=float, default=1800,
                        help="maximum seconds per build and run (default: 1800)")
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be positive and finite")
    selected = list(dict.fromkeys(args.backend or BACKENDS))
    args.output = args.output.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix="comparison-", dir=args.output.parent))
    report = {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "scope": "host architecture prototypes; simulated peripheral, no MCU/DMA timing validation",
        "environment": {"platform": platform.platform(), "machine": platform.machine(),
                        "python": platform.python_version()},
        "expected_workload": expected_workload(),
        "common_checks": list(COMMON_CHECKS),
        "backends": {},
        "limitations": [
            "Backend timings have different harness boundaries; this report does not rank performance.",
            "Tracked memory is not whole-process or whole-board memory; read each backend's exclusions.",
            "Host correctness does not prove bounded loading, MCU performance, DMA safety, or Wi-Fi capacity.",
            "The four source-language frontends and their compatibility semantics are not implemented here.",
        ],
    }
    for name in selected:
        print(f"Running {name}; log: {run_dir / (name + '.log')}", flush=True)
        record = execute_backend(name, run_dir, args.timeout)
        report["backends"][name] = record
        print(f"{name}: {record['status']}", flush=True)
        for error in record.get("errors", []):
            print(f"  {error}", file=sys.stderr, flush=True)
        # Keep already completed evidence if a later build is interrupted.
        report["status"] = "incomplete"
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    statuses = [item["status"] for item in report["backends"].values()]
    report["status"] = ("failed" if "failed" in statuses else
                        "unavailable" if "unavailable" in statuses else "passed")
    report["all_three_executed"] = set(selected) == set(BACKENDS)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Saved {args.output}")
    return {"passed": 0, "failed": 1, "unavailable": 2}[report["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
