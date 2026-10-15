"""Real-service host protocol harness. Memory measurements belong to firmware, not this JIT."""
import errno
import socket
import struct
import time
from pathlib import Path
import wasmtime

NAMESPACE = "lightvm.experimental.v1"


class Guest:
    def __init__(self, module, real_network=False):
        self.engine = wasmtime.Engine()
        self.store = wasmtime.Store(self.engine)
        self.linker = wasmtime.Linker(self.engine)
        self.real_network = real_network
        self.socket = None
        self.clock = 0
        self.handle = 0
        self.connected = 1
        self.rx = bytearray()
        self.tx = bytearray()
        self.read_limit = self.write_limit = 2048
        self.write_blocked = False
        self.generation, self.revision, self.speed = 7, 0, 0.1
        self.commands = self.rejected = self.result = 0
        self.pending = None
        self.frames = 0
        self.identity = "host-mqtt-test"
        self.command_topic = f"lightvm/{self.identity}/speed/set"
        i32, i64, f64 = wasmtime.ValType.i32(), wasmtime.ValType.i64(), wasmtime.ValType.f64()
        imports = {
            "tcp_connect": ([i32, i32], [i32], self.connect),
            "tcp_state": ([i32], [i32], self.state),
            "tcp_read": ([i32, i32, i32], [i32], self.read),
            "tcp_write": ([i32, i32, i32], [i32], self.write),
            "tcp_close": ([i32], [i32], self.close_connection),
            "time_ms": ([], [i64], lambda caller: int(time.monotonic() * 1000) if real_network else self.clock),
            "identity": ([i32, i32], [i32], self.device_identity),
            "animation_state": ([i32, i32], [i32], self.animation_state),
            "speed_set": ([f64, i32, i32], [i32], self.speed_set),
        }
        for name, (parameters, results, callback) in imports.items():
            self.linker.define_func(NAMESPACE, name, wasmtime.FuncType(parameters, results), callback, access_caller=True)
        self.instance = self.linker.instantiate(self.store, wasmtime.Module.from_file(self.engine, str(module)))
        self.exports = self.instance.exports(self.store)
        assert self.exports["memory"].size(self.store) == 1

    def start(self, ip="127.0.0.1", port=18883):
        numeric = int.from_bytes(socket.inet_aton(ip), "big")
        if numeric >= 2**31:
            numeric -= 2**32
        self.exports["service_init"](self.store, numeric, port)

    def step(self, ms=1):
        self.clock += ms
        state = self.exports["service_step"](self.store)
        if self.pending:
            speed, generation, revision = self.pending
            self.pending = None
            if generation == self.generation and revision == self.revision and -10 <= speed <= 10:
                self.speed = speed
                self.revision += 1
                self.commands += 1
                self.result = 1
            else:
                self.rejected += 1
                self.result = 2
        self.frames += 1
        return state

    def connect(self, caller, ip, port):
        self.handle += 1
        if self.real_network:
            self.socket = socket.socket()
            self.socket.setblocking(False)
            result = self.socket.connect_ex((socket.inet_ntoa((ip & 0xffffffff).to_bytes(4, "big")), port))
            if result not in (0, errno.EINPROGRESS):
                self.socket.close()
                self.socket = None
                return -1
        return self.handle

    def state(self, caller, handle):
        if handle != self.handle:
            return -1
        if self.real_network:
            import select
            if self.socket is None or self.socket.fileno() < 0:
                return -1
            if not select.select([], [self.socket], [], 0)[1]:
                return 0
            return -1 if self.socket.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR) else 1
        return self.connected

    def read(self, caller, handle, offset, size):
        if handle != self.handle:
            return -1
        size = min(size, self.read_limit)
        if self.real_network:
            try:
                data = self.socket.recv(size)
                if not data:
                    return -1
            except BlockingIOError:
                return 0
            except OSError:
                return -1
        else:
            data = self.rx[:size]
            del self.rx[:len(data)]
        caller.get("memory").write(caller, data, offset)
        return len(data)

    def write(self, caller, handle, offset, size):
        if handle != self.handle:
            return -1
        if self.write_blocked:
            return 0
        size = min(size, self.write_limit)
        data = caller.get("memory").read(caller, offset, offset + size)
        if self.real_network:
            try:
                return self.socket.send(data)
            except BlockingIOError:
                return 0
            except OSError:
                return -1
        self.tx.extend(data)
        return size

    def close_connection(self, caller=None, handle=None):
        if self.socket:
            self.socket.close()
            self.socket = None
        return 0

    def device_identity(self, caller, offset, capacity):
        data = self.identity.encode() + b"\0"
        assert len(data) <= capacity
        caller.get("memory").write(caller, data, offset)
        return len(data) - 1

    def animation_state(self, caller, offset, size):
        assert size == 40
        caller.get("memory").write(caller, struct.pack("<IIdIIIIII", self.generation, self.revision,
            self.speed, self.frames, 100000, 500, self.result, self.commands, self.rejected), offset)
        return 0

    def speed_set(self, caller, speed, generation, revision):
        if self.pending:
            return -2
        self.pending = (speed, generation & 0xffffffff, revision & 0xffffffff)
        return 0


def packet(kind, body=b""):
    length = len(body)
    encoded = bytearray([kind])
    while True:
        digit = length % 128
        length //= 128
        encoded.append(digit | (128 if length else 0))
        if not length:
            return bytes(encoded) + body


def publish(topic, payload, qos=0, identity=1, duplicate=False):
    name = topic.encode()
    body = struct.pack(">H", len(name)) + name
    if qos:
        body += struct.pack(">H", identity)
    return packet(0x30 | qos * 2 | (8 if duplicate else 0), body + payload)


def packets(buffer):
    result = []
    while len(buffer) >= 2:
        index, length, multiplier = 1, 0, 1
        while index < len(buffer):
            digit = buffer[index]
            index += 1
            length += (digit & 127) * multiplier
            multiplier *= 128
            if not digit & 128:
                break
        else:
            break
        if len(buffer) < index + length:
            break
        result.append((buffer[0], bytes(buffer[index:index + length])))
        del buffer[:index + length]
    return result


class Broker:
    """Deterministic broker exposing wire fragmentation and ACK/queue control."""
    def __init__(self, guest):
        self.guest = guest
        self.received = []
        self.auto_ack = True

    def tick(self, ms=1):
        state = self.guest.step(ms)
        for kind, body in packets(self.guest.tx):
            self.received.append((kind, body))
            if kind == 0x10:
                self.guest.rx += packet(0x20, b"\0\0")
            elif kind == 0x82:
                self.guest.rx += packet(0x90, body[:2] + b"\1\0")
            elif kind & 0xf0 == 0x30 and kind & 6 == 2 and self.auto_ack:
                length = int.from_bytes(body[:2], "big")
                self.guest.rx += packet(0x40, body[2 + length:4 + length])
            elif kind == 0xc0 and self.auto_ack:
                self.guest.rx += packet(0xd0)
        return state

    def ready(self):
        self.guest.start()
        for _ in range(1000):
            self.tick()
            pubs = [b for k, b in self.received if k & 0xf0 == 0x30]
            if len(pubs) >= 7:
                return
        raise AssertionError("MQTT handshake/discovery did not complete")
