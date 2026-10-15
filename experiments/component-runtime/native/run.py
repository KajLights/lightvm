#!/usr/bin/env python3
"""Build and exercise the direct-native component candidate on this host."""
from __future__ import annotations

import argparse
import ctypes as C
import json
from pathlib import Path
import platform
import statistics
import sys
import time
import traceback

sys.dont_write_bytecode = True
from runtime import (  # noqa: E402
    BUILD, BOUNDS, CANCELLED, DONE, IDLE, INIT_FAILED, PIXELS, QUANTUM, READY,
    READONLY, RENDER, SEED, STALE, STATE_ERROR, YIELD, AsyncOutput, Benchmark,
    Core, Guard, Instance, Ledger, Module, Rejected, SocketService, State, build,
    sha256,
)


def reference(frame, seed=SEED, pixels=PIXELS):
    x = seed ^ ((frame * 0x9e3779b9) & 0xffffffff)
    result = bytearray()
    for i in range(pixels):
        x = (x ^ (x << 13)) & 0xffffffff
        x = (x ^ (x >> 17)) & 0xffffffff
        x = (x ^ (x << 5)) & 0xffffffff
        x = (x + i + 0x6d2b79f5) & 0xffffffff
        result.extend((x & 255, (x >> 8) & 255, (x >> 16) & 255))
    return bytes(result)


def fnv(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def distribution(values):
    ordered = sorted(values)
    if not ordered:
        return {"samples": 0}
    return {"samples": len(values), "min_ns": ordered[0],
            "median_ns": statistics.median(ordered),
            "p95_ns": ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))],
            "max_ns": ordered[-1]}


def rejected(callable_, exception=Rejected):
    try:
        result = callable_()
    except exception:
        return True
    if isinstance(result, Module):
        result.close()
    if isinstance(result, Instance):
        result.close()
        result.buffer.dispose_guard()
    return False


def malformed_checks(manifest_path):
    manifest = json.loads(manifest_path.read_text())
    results = {}
    cases = {
        "unsupported_abi": {**manifest, "abi_version": 999},
        "digest_mismatch": {**manifest, "sha256": "0" * 64},
        "oversized_state": {**manifest, "state_bytes": 1000000},
        "path_escape": {**manifest, "artifact": "../outside.dylib"},
        "undeclared_capability": {**manifest, "capabilities": ["raw-memory"]},
    }
    for name, package in cases.items():
        path = BUILD / ("bad-" + name + ".json")
        path.write_text(json.dumps(package))
        results[name] = rejected(lambda: Module(path))
    invalid = BUILD / "invalid-native-artifact.bin"
    invalid.write_bytes(b"this is not executable native code\n")
    path = BUILD / "bad-binary.json"
    path.write_text(json.dumps({**manifest, "artifact": invalid.name, "sha256": sha256(invalid)}))
    results["invalid_executable"] = rejected(lambda: Module(path))
    path = BUILD / "bad-json.json"
    path.write_text("{")
    results["invalid_json"] = rejected(lambda: Module(path))
    return results


def execute(build_info):
    checks, modules, instances, all_step_times = {}, [], [], []
    retired_buffers = []
    ledger = Ledger(4096)
    core = Core(build_info["core"])
    output = AsyncOutput(ledger, core)
    service = SocketService()
    frame_completion_times = []
    preparation_times = []
    scheduler_times = []
    lifecycle = {}
    workload = {"pixels": PIXELS, "seed": SEED, "frame_hashes": []}
    kernel = Benchmark()
    try:
        module = Module(build_info["manifests"][0])
        modules.append(module)
        lifecycle["initial_validation_ns"] = module.validation_ns
        lifecycle["initial_load_ns"] = module.load_ns

        def make(module=module, seed=SEED, mode=0):
            instance = Instance(module, ledger, seed, mode)
            instances.append(instance)
            return instance

        first, second = make(), make(seed=SEED + 1)
        for _ in range(5):
            first_rc, second_rc = first.prepare_step(), second.prepare_step()
            service.tick()
        assert first_rc == second_rc == DONE
        lifecycle["initial_instances_initialization_ns"] = first.init_ns + second.init_ns
        checks["incremental_initialization"] = first.init_history == [64, 48, 32, 16, 0]

        # The effect and core receive exactly the same pointer. Python readbacks
        # below are diagnostics, not part of the render/transform/output path.
        address = first.buffer.address
        view = first.buffer.view()
        fill_rc = core.lib.core_fill(C.byref(view), 3, 4, 5)
        fill_ok = first.buffer.bytes() == bytes((3, 4, 5)) * PIXELS
        dim_rc = core.lib.core_dim(C.byref(view))
        checks["native_bulk_operation"] = (fill_rc == dim_rc == DONE and fill_ok
                                            and first.buffer.bytes() == bytes((1, 2, 2)) * PIXELS)
        checks["native_buffer_alias"] = C.cast(view.data, C.c_void_p).value == address

        continuation_trace = []
        for frame in range(3):
            assert first.begin(frame) == YIELD
            while True:
                before = first.state.cursor
                rc = first.step(17)
                continuation_trace.append(first.state.cursor)
                assert first.state.cursor > before
                if rc != YIELD:
                    break
            assert rc == DONE
            workload["frame_hashes"].append(f"{core.hash(first.buffer):08x}")
            assert first.buffer.bytes() == reference(frame)
        checks["resumable_execution"] = (continuation_trace[:3] == [17, 34, 51]
                                          and first.state.total_work == 3 * PIXELS
                                          and first.buffer.address == address)

        assert first.begin(3) == second.begin(4) == YIELD
        first.step(7)
        checks["partial_frame_not_published"] = rejected(lambda: output.submit(first.buffer, 0))
        first_state = (first.state.cursor, first.state.x, first.state.total_work)
        second.step(11)
        checks["independent_instances"] = (
            first_state == (first.state.cursor, first.state.x, first.state.total_work)
            and second.state.cursor == 11 and first.state.cursor == 7
            and C.addressof(first.state) != C.addressof(second.state)
            and first.buffer.address != second.buffer.address
            and first.module is second.module)
        while first.state.phase == RENDER:
            assert first.step() in (YIELD, DONE)
        while second.state.phase == RENDER:
            assert second.step() in (YIELD, DONE)
        checks["independent_instances"] &= (first.buffer.bytes() == reference(3)
                                             and second.buffer.bytes() == reference(4, SEED + 1))

        # Missing bounds and stale-lease checks must fail before writes.
        assert first.begin(10) == YIELD
        saved = first.buffer.bytes()
        short = first.buffer.view()
        short.bytes -= 3
        short_rc = first.step(view=short)
        first.state.cursor = PIXELS
        cursor_rc = first.step()
        checks["bounds_rejection"] = (short_rc == cursor_rc == BOUNDS
                                       and first.buffer.bytes() == saved)
        first.state.cursor = 0
        stale = first.buffer.view()
        first.buffer.guard.generation += 1
        stale_rc = first.step(view=stale)
        checks["stale_view_rejection"] = stale_rc == STALE and first.buffer.bytes() == saved
        while first.state.phase == RENDER:
            assert first.step() in (YIELD, DONE)

        # Preparation failure rolls back its own resources. Existing programs
        # and local I/O continue during each preparation quantum.
        before_failed = ledger.current
        bad = make(mode=2)
        received_before = service.received
        secondary_work_before = second.state.total_work
        assert second.begin(5) == YIELD
        for _ in range(5):
            rc = bad.prepare_step()
            if second.state.phase == RENDER:
                second.step()
            service.tick()
        assert rc == INIT_FAILED
        lifecycle["failed_initialization_ns"] = bad.init_ns
        bad.close()
        bad.buffer.dispose_guard()
        assert first.render(11) == DONE
        checks["failed_replacement_preserves_old"] = (
            ledger.current == before_failed and first.buffer.bytes() == reference(11)
            and second.state.total_work > secondary_work_before
            and service.received > received_before)

        # Publish a complete frame, then replace its owner while output retains
        # the old allocation. No RGB copy is made by submission or completion.
        assert first.state.phase == READY
        old_buffer = first.buffer
        old_address, old_hash = old_buffer.address, core.hash(old_buffer)
        old_view = old_buffer.view()
        output.submit(old_buffer, 0, delay=30)
        readonly_view = old_buffer.view()
        checks["write_while_retained_rejected"] = (
            first.begin(12) == READONLY
            and core.lib.core_fill(C.byref(readonly_view), 255, 0, 0) == READONLY
            and core.lib.core_fill(C.byref(old_view), 255, 0, 0) == STALE)

        # Explicit, admitted buffer rotation lets the OLD effect keep rendering
        # during preparation while its already-published allocation is retained.
        retired_buffers.append(first.rotate_buffer())
        old_work_before_preparation = first.state.total_work
        assert first.begin(12) == YIELD

        replacement_module = Module(build_info["manifests"][1])
        modules.append(replacement_module)
        lifecycle["replacement_validation_ns"] = replacement_module.validation_ns
        lifecycle["replacement_load_ns"] = replacement_module.load_ns
        replacement = make(module=replacement_module, seed=SEED + 2)
        received_before = service.received
        second_before = second.state.total_work
        second_frame = 6
        for tick in range(1, 6):
            start = time.perf_counter_ns()
            rc = replacement.prepare_step()
            preparation_times.append(time.perf_counter_ns() - start)
            first.step()
            if second.state.phase == READY:
                assert second.begin(second_frame) == YIELD
                second_frame += 1
            second.step()
            service.tick()
            output.poll(tick)
        assert rc == DONE
        lifecycle["replacement_initialization_ns"] = replacement.init_ns
        checks["preparation_allows_progress"] = (
            service.received > received_before and second.state.total_work > second_before
            and old_buffer.guard.alive and core.hash(old_buffer) == old_hash
            and first.state.total_work - old_work_before_preparation == PIXELS
            and first.buffer.bytes() == reference(12))
        checks["buffer_rotation"] = (first.buffer.address != old_buffer.address
                                      and not old_buffer.owner and old_buffer.consumers == 1)
        activation_start = time.perf_counter_ns()
        assert replacement.state.phase == IDLE
        active = replacement
        first.close()
        lifecycle["activation_and_old_teardown_ns"] = time.perf_counter_ns() - activation_start
        checks["replacement"] = (active is replacement and first.closed
                                  and old_buffer.guard.alive and old_buffer.consumers == 1
                                  and old_buffer.address == old_address
                                  and module.manifest["artifact_revision"] == 1
                                  and replacement_module.manifest["artifact_revision"] == 2)
        checks["module_unload_with_instances_rejected"] = rejected(module.close)

        active_frame = 0
        frame_completions = 0
        retained_ticks = 0
        for tick in range(6, 46):
            tick_start = time.perf_counter_ns()
            for instance, seed in ((active, (SEED + 2) ^ 0x13579bdf), (second, SEED + 1)):
                if instance.state.phase in (IDLE, READY):
                    if instance is active:
                        frame = active_frame
                        active_frame += 1
                    else:
                        frame = second_frame
                        second_frame += 1
                    assert instance.begin(frame) == YIELD
                rc = instance.step()
                if rc == DONE:
                    assert instance.buffer.bytes() == reference(instance.state.frame, seed)
                    frame_completions += 1
                    if instance is active:
                        frame_completion_times.append(time.perf_counter_ns())
            service.tick()
            output.poll(tick)
            if tick < 30:
                retained_ticks += 1
                assert old_buffer.guard.alive and core.hash(old_buffer) == old_hash
            scheduler_times.append(time.perf_counter_ns() - tick_start)
        checks["async_output_retention"] = (
            output.valid and output.completed == 1 and output.copies == 0
            and not old_buffer.guard.alive and frame_completions >= 10
            and retained_ticks == 24)
        checks["stale_view_rejection"] &= (
            core.lib.core_fill(C.byref(old_view), 1, 2, 3) == STALE)
        # No stale view survives metadata reclamation.
        del view, short, stale, old_view, readonly_view
        old_buffer.dispose_guard()

        # Runaway source code yields at a compiler-inserted backedge. The host
        # cancels after a cumulative 512 work units, while another task runs.
        runaway = make(mode=1)
        assert runaway.prepare() == DONE and runaway.begin(0) == YIELD
        work_deltas = []
        received_before = service.received
        for _ in range(8):
            previous = runaway.state.total_work
            assert runaway.step(1000000) == YIELD  # hard clamped to 64 by C
            work_deltas.append(runaway.state.total_work - previous)
            if active.state.phase == READY:
                assert active.begin(active_frame) == YIELD
                active_frame += 1
            active.step()
            service.tick()
        runaway.cancel()
        checks["runaway_bounded"] = (
            work_deltas == [QUANTUM] * 8 and runaway.state.total_work == 512
            and service.received > received_before and runaway.state.phase == CANCELLED)
        checks["cancellation"] = runaway.step() == STATE_ERROR
        runaway.close()
        runaway.buffer.dispose_guard()

        # Reserve fails after state and RGB allocations but before guard storage;
        # partially constructed instances must return all reservation credits.
        previous = ledger.current
        old_limit = ledger.limit
        ledger.limit = previous + C.sizeof(State) + PIXELS * 3
        admission_rejected = rejected(lambda: Instance(module, ledger), MemoryError)
        ledger.limit = old_limit
        no_leak_after_admission = ledger.current == previous
        ledger.fail_next = True
        allocation_rejected = rejected(lambda: Instance(module, ledger), MemoryError)
        checks["admission_failure"] = admission_rejected and no_leak_after_admission
        checks["allocation_failure"] = allocation_rejected and ledger.current == previous

        # Benchmark loop executes in C; Python/ctypes does not dispatch pixels or
        # quanta in this measurement. Clock reads and ABI calls are included.
        benchmark_instance = make()
        assert benchmark_instance.prepare() == DONE
        benchmark_view = benchmark_instance.buffer.view()
        assert core.lib.core_benchmark(
            C.cast(module.lib.native_begin, C.c_void_p),
            C.cast(module.lib.native_step, C.c_void_p),
            C.byref(benchmark_instance.state), C.byref(benchmark_view),
            2000, QUANTUM, C.byref(kernel)) == DONE
        checks["kernel_benchmark_correct"] = kernel.checksum == fnv(reference(1999))
        benchmark_instance.close()
        del benchmark_view
        benchmark_instance.buffer.dispose_guard()

        malformed = malformed_checks(build_info["manifests"][0])
        checks["malformed_artifact_rejected"] = all(malformed.values())
        for _ in range(32):
            service.tick(produce=False)
        checks["service_progress"] = (service.ordered and service.received == service.sent
                                       and service.received > 50
                                       and service.max_received_per_tick <= 2
                                       and service.peak_pending <= 32 and service.dropped > 0)
        for instance in instances:
            all_step_times.extend(instance.step_ns)
        gaps = [b - a for a, b in zip(frame_completion_times, frame_completion_times[1:])]
        metrics = {
            "timing_units": "nanoseconds",
            "lifecycle": lifecycle,
            "kernel": {"frames": kernel.frames, "pixels_per_frame": PIXELS,
                       "wall_ns": kernel.elapsed_ns, "process_cpu_ns": kernel.cpu_ns,
                       "wall_ns_per_frame": kernel.elapsed_ns / kernel.frames,
                       "cpu_ns_per_frame": kernel.cpu_ns / kernel.frames,
                       "maximum_step_ns": kernel.maximum_step_ns, "steps": kernel.steps,
                       "includes": "C ABI dispatch, checks and clock reads; excludes Python dispatch"},
            "host_ctypes_step": distribution(all_step_times),
            "mixed_scheduler_tick": distribution(scheduler_times),
            "mixed_frame_gaps": distribution(gaps),
            "preparation_quantum": distribution(preparation_times),
            "communication_tick": distribution(service.tick_ns),
            "native_payload_memory": {
                "admission_limit_bytes": ledger.limit, "peak_bytes": ledger.peak,
                "peak_allocations": ledger.peak_allocations,
                "largest_requested_allocation_bytes": ledger.largest_allocation,
                "state_bytes_per_instance": C.sizeof(State),
                "rgb_bytes_per_instance": PIXELS * 3,
                "guard_bytes_per_instance": C.sizeof(Guard),
                "output_queue_capacity": output.capacity,
                "output_queue_bytes": C.sizeof(output.records),
                "scope": "native payload reservations only; excludes Python, dylib mappings, host stacks and OS sockets",
            },
            "output": {"implementation": "simulated completion ticks, no physical peripheral",
                       "data_path_copies": output.copies,
                       "diagnostic_readback_copies": "excluded from data path; Python checks read native bytes",
                       "peak_retained_bytes": output.peak_retained_bytes,
                       "peak_retained_allocations": output.peak_retained_allocations,
                       "rotation_allocations": 1,
                       "rotation_bytes": PIXELS * 3,
                       "completed": output.completed},
            "communications": {"implementation": "real local SOCK_DGRAM socketpair",
                               "sent": service.sent, "received": service.received,
                               "dropped_by_bounded_producer": service.dropped,
                               "peak_outstanding": service.peak_pending,
                               "maximum_messages_per_service_tick": service.max_received_per_tick},
            "continuation_cursor_prefix": continuation_trace[:5],
            "runaway_work_per_step": work_deltas,
            "malformed_cases": malformed,
            "compiler_stack_usage_bytes": build_info["stack_usage_bytes"],
            "component_stack_policy_bytes": 512,
        }
    finally:
        output.close()
        for instance in reversed(instances):
            instance.close()
            instance.buffer.dispose_guard()
        for buffer in retired_buffers:
            buffer.dispose_guard()
        for module in reversed(modules):
            module.close()
        service.close()
    checks["cleanup"] = ledger.current == 0 and ledger.allocations == 0 and service.closed
    metrics["native_payload_memory"]["remaining_bytes_after_cleanup"] = ledger.current
    metrics["native_payload_memory"]["remaining_allocations_after_cleanup"] = ledger.allocations
    return checks, workload, metrics


LIMITATIONS = [
    "Host-only C11 shared libraries on the recorded architecture; no MCU loader, flash write, DMA or device timing has been tested.",
    "Native code is trusted output of an audited restricted producer. Manifest/digest/ABI checks are compatibility and corruption checks, not authentication or a verifier. Arbitrary native binaries can bypass checks, corrupt memory, run constructors or block indefinitely.",
    "Bounds, lease, fuel and no-recursion checks are present in this hand-written reference component. Four language frontends and a compiler that enforces these rules are not implemented.",
    "Scheduling is cooperative on a single worker. Every component loop has a bounded backedge; uncooperative native code and OS/dlopen calls cannot be preempted here. Measured maxima are observations, not hard real-time bounds.",
    "Package validation and dlopen remain synchronous; initialization is incremental. Load stalls are measured separately, and uninterrupted service during loading is not established.",
    "Native payload admission covers state, RGB allocations, stable guard metadata and a fixed output queue. Python objects, interpreter, mappings, SDK memory, host stacks, sockets and allocator overhead are outside that budget; largest free block and full-process peak RAM are not measured.",
    "Compiler .su evidence checks static component stack frames against 512 bytes for a known nonrecursive leaf call graph; it excludes ctypes and resident-core stacks. It is not verification of arbitrary machine code.",
    "The two initial effects share code and own separate RGB allocations. One additional admitted 900-byte RGB allocation permits the old effect to render during replacement preparation while output retains its earlier frame. No claim of continuous output from one buffer or composition without extra storage.",
    "Output is simulated; socketpair traffic is real local I/O but does not exercise Wi-Fi, MQTT, commissioning, reconnects or protocol-stack replacement. There are no configured physical frame deadlines or deadline-miss claims.",
    "Views and guards use single-worker ownership rules, not atomic multicore synchronization. Metadata must outlive all stale views; guard reclamation is explicit after views are discarded.",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = {"schema_version": 1, "backend": "direct-native", "status": "failed",
              "environment": {"system": platform.system(), "machine": platform.machine(),
                              "python": platform.python_version(), "host_only": True},
              "workload": {"pixels": PIXELS, "seed": SEED, "frame_hashes": []},
              "checks": {}, "metrics": {}, "limitations": LIMITATIONS}
    try:
        info = build()
        result["environment"].update({"compiler": info["compiler"],
                                       "build_commands": info["commands"],
                                       "artifacts": info["artifacts"]})
        checks, workload, metrics = execute(info)
        result.update(checks=checks, workload=workload, metrics=metrics)
        result["status"] = "passed" if all(checks.values()) else "failed"
    except (FileNotFoundError, PermissionError) as exc:
        result["status"] = "unavailable"
        result["error"] = str(exc)
    except Exception as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
        result["traceback"] = traceback.format_exc()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"backend": result["backend"], "status": result["status"],
                      "output": str(args.output), "error": result.get("error")}))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
