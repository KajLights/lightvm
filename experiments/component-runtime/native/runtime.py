"""Host-only native component harness. Trusted code; no process sandbox."""
from __future__ import annotations

import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import socket
import struct
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / "build/component-runtime/native"
YIELD, DONE = 0, 1
BOUNDS, STALE, READONLY, STATE_ERROR, INIT_FAILED = -1, -2, -3, -4, -5
NEW, IDLE, RENDER, READY, CANCELLED = range(5)
QUANTUM, PIXELS, SEED = 64, 300, 0x12345678


class Guard(C.Structure):
    _fields_ = [("generation", C.c_uint64), ("writable", C.c_uint32),
                ("alive", C.c_uint32)]


class View(C.Structure):
    _fields_ = [("data", C.POINTER(C.c_uint8)), ("bytes", C.c_size_t),
                ("generation", C.c_uint64), ("guard", C.POINTER(Guard))]


class State(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ("seed", "frame", "cursor", "x", "pixels", "phase", "mode",
                 "init_remaining")] + [("total_work", C.c_uint64)]


class Info(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ("abi_version", "descriptor_bytes", "state_bytes", "max_step_work",
                 "stack_limit_bytes", "recursion_allowed", "heap_allowed", "artifact_revision")]


class Benchmark(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in
                ("elapsed_ns", "cpu_ns", "maximum_step_ns", "steps")] + [
                    ("frames", C.c_uint32), ("checksum", C.c_uint32)]


class OutputRecord(C.Structure):
    _fields_ = [("address", C.c_uint64), ("due", C.c_uint64),
                ("checksum", C.c_uint32), ("active", C.c_uint32)]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def build():
    """Build distinct immutable artifacts, with compiler stack-use evidence."""
    BUILD.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which(os.environ.get("CC", "cc"))
    if not compiler:
        raise FileNotFoundError("A host C11 compiler is required (CC or cc)")
    version = subprocess.check_output([compiler, "--version"], text=True).splitlines()[0]
    extension = ".dylib" if platform.system() == "Darwin" else ".so"
    link_flag = "-dynamiclib" if platform.system() == "Darwin" else "-shared"
    flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC",
             "-fstack-usage", "-fno-optimize-sibling-calls"]
    commands, manifests, stack_reports = [], [], {}
    # Version 2 is separately linked and loaded: code can change in a real update.
    for name, source in [("effect-v1", "component.c"), ("effect-v2", "component.c"),
                         ("core", "core.c")]:
        object_file = BUILD / (name + ".o")
        artifact = BUILD / (name + extension)
        revision = 2 if name == "effect-v2" else 1
        compile_command = [compiler, *flags, f"-DNATIVE_REVISION={revision}",
                           "-c", str(HERE / source), "-o", str(object_file)]
        link_command = [compiler, link_flag, str(object_file), "-o", str(artifact)]
        for command in (compile_command, link_command):
            subprocess.run(command, check=True, capture_output=True, text=True)
            commands.append(command)
        stack_file = object_file.with_suffix(".su")
        if not stack_file.exists():
            raise RuntimeError("Compiler did not emit required -fstack-usage evidence")
        stack = {}
        for line in stack_file.read_text().splitlines():
            columns = line.split("\t")
            if len(columns) != 3 or columns[2] != "static":
                raise RuntimeError(f"Unbounded/dynamic stack report: {line}")
            stack[columns[0].split(":")[-1]] = int(columns[1])
        stack_reports[name] = stack
        if name == "core":
            continue
        if not stack or max(stack.values()) > 512:
            raise RuntimeError("Native component violates the fixed 512-byte stack policy")
        manifest = {
            "schema_version": 1, "abi_version": 1, "kind": "native-component",
            "name": name, "artifact": artifact.name, "sha256": sha256(artifact),
            "artifact_revision": revision,
            "target": {"system": platform.system(), "machine": platform.machine()},
            "state_bytes": C.sizeof(State), "buffer_bytes": PIXELS * 3,
            "max_step_work": QUANTUM, "stack_limit_bytes": 512,
            "recursion_allowed": False, "heap_allowed": False,
            "capabilities": ["rgb8.write"], "producer": "audited-reference-v1",
        }
        manifest_path = BUILD / (name + ".json")
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        manifests.append(manifest_path)
    return {
        "compiler": version, "compiler_path": compiler, "commands": commands,
        "manifests": manifests, "core": BUILD / ("core" + extension),
        "stack_usage_bytes": stack_reports,
        "artifacts": [{"path": str(path.relative_to(ROOT)), "sha256": sha256(path),
                       "file_bytes": path.stat().st_size}
                      for path in sorted(BUILD.glob("*" + extension))],
    }


class Rejected(ValueError):
    pass


class Ledger:
    """Only known native payload allocations, never total resident memory."""
    def __init__(self, limit=4096):
        self.limit = limit
        self.current = self.peak = self.allocations = self.peak_allocations = 0
        self.largest_allocation = 0
        self.fail_next = False

    def reserve(self, size):
        if self.fail_next:
            self.fail_next = False
            raise MemoryError("injected native allocation failure")
        if self.current + size > self.limit:
            raise MemoryError("native payload admission budget exceeded")
        self.current += size
        self.allocations += 1
        self.peak = max(self.peak, self.current)
        self.peak_allocations = max(self.peak_allocations, self.allocations)
        self.largest_allocation = max(size, self.largest_allocation)

    def release(self, size):
        self.current -= size
        self.allocations -= 1
        assert self.current >= 0 and self.allocations >= 0


class Buffer:
    def __init__(self, ledger, size=PIXELS * 3):
        self.ledger, self.size = ledger, size
        ledger.reserve(size)
        guard_reserved = False
        try:
            self._data = (C.c_uint8 * size)()
            ledger.reserve(C.sizeof(Guard))
            guard_reserved = True
            self.guard = Guard(1, 1, 1)
        except BaseException:
            if guard_reserved:
                ledger.release(C.sizeof(Guard))
            ledger.release(size)
            self._data = None
            raise
        self.address = C.addressof(self._data)
        self.owner = True
        self.consumers = 0
        self.guard_accounted = True
        self.complete_frame = False

    def view(self):
        # Cast an integer address: no hidden ctypes reference retains RGB data.
        return View(C.cast(self.address, C.POINTER(C.c_uint8)), self.size,
                    self.guard.generation, C.pointer(self.guard))

    def bytes(self):
        if not self.guard.alive:
            raise Rejected("read of released buffer")
        return C.string_at(self.address, self.size)

    def retain(self):
        if not self.owner or not self.guard.alive or self.consumers:
            raise Rejected("buffer is not available for publication")
        self.guard.generation += 1
        self.guard.writable = 0
        self.consumers += 1

    def complete(self):
        if not self.consumers:
            raise Rejected("duplicate completion")
        self.consumers -= 1
        self.guard.generation += 1
        if self.owner:
            self.guard.writable = 1
        else:
            self._free()

    def release_owner(self):
        if not self.owner:
            return
        self.owner = False
        self.guard.generation += 1
        self.guard.writable = 0
        if not self.consumers:
            self._free()

    def _free(self):
        if self.guard.alive:
            self.guard.alive = 0
            self._data = None
            self.ledger.release(self.size)
        # Keep guard storage while the Buffer or stale View is reachable. Its
        # ctypes allocation remains accounted until explicit dispose_guard().

    def dispose_guard(self):
        if not self.guard_accounted:
            return
        if self.guard.alive or self.consumers:
            raise Rejected("guard cannot be disposed while buffer is live")
        if self.guard_accounted:
            self.ledger.release(C.sizeof(Guard))
            self.guard_accounted = False
            self.guard = None


class Core:
    def __init__(self, path):
        self.lib = C.CDLL(str(path))
        self.lib.core_hash.argtypes = [C.POINTER(C.c_uint8), C.c_size_t]
        self.lib.core_hash.restype = C.c_uint32
        self.lib.core_fill.argtypes = [C.POINTER(View), C.c_uint8, C.c_uint8, C.c_uint8]
        self.lib.core_fill.restype = C.c_int
        self.lib.core_dim.argtypes = [C.POINTER(View)]
        self.lib.core_dim.restype = C.c_int
        self.lib.core_benchmark.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(State),
                                           C.POINTER(View), C.c_uint32, C.c_uint32,
                                           C.POINTER(Benchmark)]
        self.lib.core_benchmark.restype = C.c_int

    def hash(self, buffer):
        if not buffer.guard.alive:
            raise Rejected("hash of released buffer")
        return self.lib.core_hash(C.cast(buffer.address, C.POINTER(C.c_uint8)), buffer.size)


class Module:
    def __init__(self, manifest_path):
        start = time.perf_counter_ns()
        self.instances = 0
        self.closed = False
        try:
            raw = Path(manifest_path).read_bytes()
            if len(raw) > 65536:
                raise Rejected("manifest too large")
            manifest = json.loads(raw)
        except (OSError, ValueError) as exc:
            raise Rejected("invalid manifest") from exc
        required = {
            "schema_version": 1, "abi_version": 1, "kind": "native-component",
            "state_bytes": C.sizeof(State), "buffer_bytes": PIXELS * 3,
            "max_step_work": QUANTUM, "stack_limit_bytes": 512,
            "recursion_allowed": False, "heap_allowed": False,
            "capabilities": ["rgb8.write"], "producer": "audited-reference-v1",
            "target": {"system": platform.system(), "machine": platform.machine()},
        }
        if not isinstance(manifest, dict) or any(
                manifest.get(k) != v or type(manifest.get(k)) is not type(v)
                for k, v in required.items()):
            raise Rejected("unsupported package contract")
        if type(manifest.get("artifact_revision")) is not int or manifest["artifact_revision"] < 1:
            raise Rejected("invalid artifact revision")
        artifact_name = manifest.get("artifact", "")
        if not isinstance(artifact_name, str) or Path(artifact_name).name != artifact_name:
            raise Rejected("artifact must be a sibling file")
        artifact = Path(manifest_path).parent / artifact_name
        if not artifact.is_file() or artifact.stat().st_size > 4 * 1024 * 1024:
            raise Rejected("missing or oversized artifact")
        if sha256(artifact) != manifest.get("sha256"):
            raise Rejected("artifact digest mismatch")
        self.validation_ns = time.perf_counter_ns() - start
        load_start = time.perf_counter_ns()
        try:
            self.lib = C.CDLL(str(artifact.resolve()))
        except OSError as exc:
            raise Rejected("host loader rejected artifact") from exc
        self.load_ns = time.perf_counter_ns() - load_start
        try:
            self.lib.native_info.argtypes = []
            self.lib.native_info.restype = C.POINTER(Info)
            info = self.lib.native_info().contents
            if (info.abi_version, info.descriptor_bytes, info.state_bytes,
                info.max_step_work, info.stack_limit_bytes, info.recursion_allowed,
                info.heap_allowed, info.artifact_revision) != (
                    1, C.sizeof(Info), C.sizeof(State), QUANTUM, 512, 0, 0,
                    manifest["artifact_revision"]):
                raise Rejected("exported ABI descriptor mismatch")
            for name, args in {
                "native_init": [C.POINTER(State), C.c_uint32, C.c_uint32, C.c_uint32],
                "native_init_step": [C.POINTER(State), C.c_uint32],
                "native_begin": [C.POINTER(State), C.c_uint32],
                "native_step": [C.POINTER(State), C.POINTER(View), C.c_uint32],
                "native_cancel": [C.POINTER(State)],
            }.items():
                function = getattr(self.lib, name)
                function.argtypes, function.restype = args, C.c_int
        except (AttributeError, Rejected) as exc:
            self.close()
            raise Rejected("invalid component exports") from exc
        self.manifest = manifest

    def close(self):
        if self.instances:
            raise Rejected("cannot unload a module with live instances")
        if not self.closed:
            import _ctypes
            _ctypes.dlclose(self.lib._handle)
            self.closed = True


class Instance:
    def __init__(self, module, ledger, seed=SEED, mode=0):
        if module.closed:
            raise Rejected("module is unloaded")
        self.module, self.ledger = module, ledger
        self.closed = False
        self.step_ns = []
        self.init_ns = 0
        self.init_steps = 0
        self.init_history = []
        self.frame_start_work = 0
        ledger.reserve(C.sizeof(State))
        try:
            self.state = State()
            self.buffer = Buffer(ledger)
        except BaseException:
            ledger.release(C.sizeof(State))
            self.state = None
            raise
        module.instances += 1
        rc = module.lib.native_init(C.byref(self.state), seed, PIXELS, mode)
        if rc != YIELD:
            self.close()
            self.buffer.dispose_guard()
            raise Rejected(f"init rejected: {rc}")

    def prepare_step(self, budget=16):
        if self.closed:
            raise Rejected("instance is closed")
        start = time.perf_counter_ns()
        rc = self.module.lib.native_init_step(C.byref(self.state), budget)
        self.init_ns += time.perf_counter_ns() - start
        self.init_steps += 1
        self.init_history.append(self.state.init_remaining)
        return rc

    def prepare(self):
        for _ in range(100):
            rc = self.prepare_step()
            if rc != YIELD:
                return rc
        raise Rejected("initialization budget exhausted")

    def begin(self, frame):
        if self.closed or not self.buffer.guard.writable:
            return READONLY
        self.frame_start_work = self.state.total_work
        rc = self.module.lib.native_begin(C.byref(self.state), frame)
        if rc == YIELD:
            self.buffer.complete_frame = False
        return rc

    def step(self, budget=QUANTUM, view=None):
        if self.closed:
            raise Rejected("instance is closed")
        view = view if view is not None else self.buffer.view()
        start = time.perf_counter_ns()
        rc = self.module.lib.native_step(C.byref(self.state), C.byref(view), budget)
        self.step_ns.append(time.perf_counter_ns() - start)
        if rc == DONE:
            self.buffer.complete_frame = True
        return rc

    def cancel(self):
        self.buffer.complete_frame = False
        return self.module.lib.native_cancel(C.byref(self.state))

    def rotate_buffer(self):
        """Reserve a second buffer before releasing ownership of published data."""
        if self.closed or self.state.phase != READY or self.buffer.consumers != 1:
            raise Rejected("rotation requires a completed, published frame")
        replacement = Buffer(self.ledger)
        previous = self.buffer
        self.buffer = replacement
        previous.release_owner()
        return previous

    def render(self, frame, quantum=QUANTUM):
        if self.begin(frame) != YIELD:
            raise Rejected("frame cannot begin")
        for _ in range(10000):
            rc = self.step(quantum)
            if rc != YIELD:
                return rc
        raise Rejected("render budget exhausted")

    def close(self):
        if self.closed:
            return
        self.cancel()
        self.buffer.release_owner()
        self.ledger.release(C.sizeof(State))
        self.state = None
        self.module.instances -= 1
        self.closed = True


class AsyncOutput:
    """Simulated peripheral; captures no pixel copy, retains native allocation."""
    def __init__(self, ledger, core, capacity=8):
        self.ledger, self.core, self.capacity = ledger, core, capacity
        self.records = (OutputRecord * capacity)()
        ledger.reserve(C.sizeof(self.records))
        self.pending = {}
        self.peak_retained_bytes = self.completed = self.cancelled = 0
        self.peak_retained_allocations = 0
        self.copies = 0
        self.valid = True
        self.closed = False

    def submit(self, buffer, tick, delay=20):
        if not buffer.complete_frame:
            raise Rejected("partial or cancelled frame cannot be published")
        slot = next((i for i in range(self.capacity) if i not in self.pending), None)
        if slot is None:
            raise Rejected("output queue is full")
        checksum = self.core.hash(buffer)
        buffer.retain()
        self.records[slot] = OutputRecord(buffer.address, tick + delay, checksum, 1)
        self.pending[slot] = buffer
        self.peak_retained_bytes = max(self.peak_retained_bytes,
                                       sum(b.size for b in self.pending.values()))
        self.peak_retained_allocations = max(self.peak_retained_allocations, len(self.pending))

    def poll(self, tick):
        for slot, buffer in list(self.pending.items()):
            record = self.records[slot]
            self.valid &= (buffer.address == record.address and buffer.guard.alive
                           and self.core.hash(buffer) == record.checksum
                           and not buffer.guard.writable)
            if tick >= record.due:
                buffer.complete()
                self.completed += 1
                record.active = 0
                del self.pending[slot]

    def close(self):
        if self.closed:
            return
        for buffer in self.pending.values():
            buffer.complete()
            self.cancelled += 1
        self.pending.clear()
        self.ledger.release(C.sizeof(self.records))
        self.records = None
        self.closed = True


class SocketService:
    """Real local datagrams, capped at 32 outstanding and two receives/tick."""
    def __init__(self):
        self.tx, self.rx = socket.socketpair(type=socket.SOCK_DGRAM)
        for sock in (self.tx, self.rx):
            sock.setblocking(False)
        self.sent = self.received = self.dropped = self.peak_pending = 0
        self.max_received_per_tick = 0
        self.ordered = True
        self.tick_ns = []
        self.closed = False

    def tick(self, produce=True):
        start = time.perf_counter_ns()
        if produce:
            for _ in range(4):
                if self.sent - self.received >= 32:
                    self.dropped += 1
                    continue
                try:
                    self.tx.send(struct.pack("!I", self.sent))
                    self.sent += 1
                except BlockingIOError:
                    self.dropped += 1
        self.peak_pending = max(self.peak_pending, self.sent - self.received)
        handled = 0
        for _ in range(2):
            try:
                data = self.rx.recv(4)
            except BlockingIOError:
                break
            self.ordered &= len(data) == 4 and struct.unpack("!I", data)[0] == self.received
            self.received += 1
            handled += 1
        self.max_received_per_tick = max(self.max_received_per_tick, handled)
        self.tick_ns.append(time.perf_counter_ns() - start)

    def close(self):
        self.tx.close()
        self.rx.close()
        self.closed = True
