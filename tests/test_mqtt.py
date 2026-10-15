import json
import os
from pathlib import Path
import struct
import unittest
import wasmtime
from experiments.mqtt.host import Guest, Broker, packet, publish, packets
from tools.client import Client, HttpTransport, ProcessTransport, ProtocolError, MANAGE, PLAY
from lightvm.compiler import compile_source

ROOT = Path(__file__).resolve().parents[1]
MODULE = Path(os.environ.get("LIGHTVM_MQTT_SERVICE", ROOT / "build/mqtt/service.wasm"))


@unittest.skipUnless(MODULE.exists(), "build experimental MQTT artifact first")
class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.guest = Guest(MODULE)
        self.broker = Broker(self.guest)
        self.broker.ready()

    def pump(self, count=100):
        for _ in range(count):
            self.broker.tick()

    def send(self, payload=b"0.3", **kwargs):
        self.guest.rx += publish(self.guest.command_topic, payload, **kwargs)
        self.pump()

    def test_one_standard_memory_page(self):
        self.assertEqual(self.guest.exports["memory"].size(self.guest.store), 1)

    def test_fragmented_and_coalesced_commands(self):
        self.guest.read_limit = 1
        self.send()
        self.assertAlmostEqual(self.guest.speed, 0.3)
        self.guest.read_limit = 2048
        self.guest.rx += b"".join(publish(self.guest.command_topic, str(i / 10).encode()) for i in range(100))
        self.pump(500)
        self.assertEqual(self.guest.commands, 101)
        self.assertAlmostEqual(self.guest.speed, 9.9)

    def test_partial_writes_preserve_packets(self):
        guest = Guest(MODULE)
        guest.write_limit = 3
        broker = Broker(guest)
        broker.ready()
        self.assertTrue(any(k == 0x10 for k, _ in broker.received))

    def test_queue_pressure_is_bounded_and_resumes(self):
        self.guest.write_blocked = True
        self.guest.rx += publish(self.guest.command_topic, b"0.7", qos=1)
        self.pump(200)
        self.assertEqual(self.guest.commands, 1)
        self.guest.write_blocked = False
        self.pump()
        self.assertTrue(any(k == 0x40 for k, _ in self.broker.received))

    def test_qos1_duplicates_are_acked_once_applied(self):
        self.send(qos=1, identity=4321)
        self.send(qos=1, identity=4321, duplicate=True)
        self.assertEqual(self.guest.commands, 1)
        acks = [b for k, b in self.broker.received if k == 0x40 and b == struct.pack(">H", 4321)]
        self.assertEqual(len(acks), 2)

    def test_packet_id_reuse_without_duplicate_is_a_new_command(self):
        self.send(qos=1, identity=4)
        self.send(b"0.8", qos=1, identity=4)
        self.assertEqual(self.guest.commands, 2)

    def test_one_outstanding_qos1_retransmits_with_duplicate(self):
        self.broker.auto_ack = False
        self.send()
        self.pump(2500)
        duplicates = [k for k, _ in self.broker.received if k & 0xf0 == 0x30 and k & 8]
        self.assertTrue(duplicates)
        self.broker.auto_ack = True
        self.pump(2500)

    def test_stale_generation_and_revision_rejected(self):
        self.send(b'{"speed":0.4,"generation":6,"revision":0}')
        self.send(b'{"speed":0.4,"generation":7,"revision":42}')
        self.assertEqual(self.guest.commands, 0)
        self.assertEqual(self.guest.rejected, 2)

    def test_valid_concurrency_envelope(self):
        self.send(b'{"speed":0.4,"generation":7,"revision":0}')
        self.assertEqual(self.guest.speed, 0.4)

    def test_bad_commands_and_bounds(self):
        for value in (b"NaN", b"Infinity", b"0.2garbage", b"{", b'{"speed":1,"speed":2}', b'{"speed":1,"unexpected":2}', b'{"speed":1,"revision":0}'):
            self.send(value)
        self.send(b"11")
        self.assertEqual(self.guest.commands, 0)
        self.assertEqual(self.guest.rejected, 1)

    def test_oversized_packet_reconnects(self):
        old = self.guest.handle
        self.guest.rx += packet(0x30, b"x" * 2048)
        self.pump(1100)
        self.assertGreater(self.guest.handle, old)

    def test_malformed_remaining_length_reconnects(self):
        old = self.guest.handle
        self.guest.rx += b"\x30\x80\x80\x80\x80"
        self.pump(1100)
        self.assertGreater(self.guest.handle, old)

    def test_invalid_qos2_publication_reconnects(self):
        old = self.guest.handle
        self.guest.rx += publish(self.guest.command_topic, b"1", qos=2)
        self.pump(1100)
        self.assertGreater(self.guest.handle, old)

    def test_keepalive_and_missing_ping_response(self):
        self.broker.tick(6000)
        self.broker.tick()
        self.assertTrue(any(k == 0xc0 for k, _ in self.broker.received))
        self.pump(10)  # Consume PINGRESP and drain the diagnostic publication.
        self.broker.auto_ack = False
        old = self.guest.handle
        self.broker.tick(6000)
        self.broker.tick()
        self.broker.tick(11000)
        self.pump(1100)
        self.assertGreater(self.guest.handle, old)

    def test_interrupted_connection_and_connect_timeout(self):
        old = self.guest.handle
        self.guest.connected = -1
        self.pump(10)
        self.guest.connected = 1
        self.pump(1100)
        self.assertGreater(self.guest.handle, old)

    def test_discovery_identity_availability_and_will(self):
        configs = []
        for kind, body in self.broker.received:
            if kind & 0xf0 != 0x30:
                continue
            n = int.from_bytes(body[:2], "big")
            topic = body[2:2+n].decode()
            if topic.startswith("homeassistant/"):
                payload = body[2+n+(2 if kind & 6 else 0):]
                config = json.loads(payload)
                self.assertEqual(config["device"]["identifiers"], [self.guest.identity])
                self.assertTrue(kind & 1)  # Retained discovery.
                self.assertIn("availability_topic", config)
                configs.append(config)
        self.assertEqual(len(configs), 4)
        connect = next(body for kind, body in self.broker.received if kind == 0x10)
        self.assertEqual(connect[6], 4)  # MQTT 3.1.1
        self.assertEqual(connect[7] & 0x2c, 0x2c)  # Retained QoS1 Last Will.
        self.assertIn(b"offline", connect)

    def test_home_assistant_birth_republishes_discovery(self):
        before = len(self.broker.received)
        self.guest.rx += publish("homeassistant/status", b"online")
        self.pump()
        self.assertGreater(len(self.broker.received), before + 4)


class FirmwareTests(unittest.TestCase):
    def setUp(self):
        executable = os.environ.get("LIGHTVM_RECEIVER", str(ROOT / "build/mqtt-host/lightvm"))
        if not Path(executable).exists():
            self.skipTest("build experimental host first")
        self.client = Client(ProcessTransport(executable))
        self.addCleanup(self.client.close)
        if not self.client.describe().get("mqtt_experiment"):
            self.skipTest("MQTT experiment disabled")
        self.client.configure({"schema":"lightvm.config/1","devices":[{"id":"strip","type":"ws2812b","config":{"pin":"GP16","count":100}}],"targets":{"A":{"device":"strip"}}})
        self.client.upload(compile_source((ROOT / "samples/python/rainbow.py").read_text()).wasm)
        self.client.require_ok(self.client.command(PLAY))

    def load(self, wat):
        self.client.upload_object("service", bytes(wasmtime.wat2wasm(wat)))
        return self.client.manage("service_load")

    def status(self):
        self.client.manage("service_status")
        return json.loads(self.client.data_reply)

    def test_real_artifact_admission_failure_keeps_animation_available(self):
        if not MODULE.exists():
            self.skipTest("build service first")
        original_heap=self.status()["heap"]
        self.client.upload_object("service", MODULE.read_bytes())
        reply = self.client.command(MANAGE, b'{"op":"service_load"}')
        if self.client.describe().get("flash_interpreter"):
            self.assertEqual(reply.code, 0, reply.message)
            self.assertTrue(self.status()["loaded"])
            self.client.manage("service_unload")
        else:
            self.assertEqual(reply.code, 3)
        self.assertEqual(self.client.status_fields()["mode"], "wasm")
        self.assertLessEqual(self.status()["heap_peak"], self.status()["heap_limit"])
        self.assertEqual(self.status()["heap"],original_heap)

    def test_deadline_fault_and_replacement_leave_animation_playing(self):
        original_heap=self.status()["heap"]
        self.load('(module (memory 1 1) (func (export "service_init") (param i32 i32)) (func (export "service_step") (result i32) (loop $again (br $again)) i32.const 0))')
        self.client.manage("service_start", ipv4=0x7f000001, port=18883)
        import time
        time.sleep(.03)
        self.assertTrue(self.status()["faulted"])
        self.assertIn("deadline", self.status()["error"])
        self.assertEqual(self.client.status_fields()["mode"], "wasm")
        self.load('(module (memory 1 1) (func (export "service_init") (param i32 i32)) (func (export "service_step") (result i32) i32.const 0))')
        self.assertFalse(self.status()["faulted"])
        self.client.manage("service_unload")
        self.assertFalse(self.status()["loaded"])
        self.assertEqual(self.status()["heap"],original_heap)

    def test_invalid_guest_buffer_fault(self):
        self.load('(module (import "lightvm.experimental.v1" "identity" (func $identity (param i32 i32) (result i32))) (memory 1 1) (func (export "service_init") (param i32 i32)) (func (export "service_step") (result i32) i32.const 65535 i32.const 65 call $identity))')
        self.client.manage("service_start", ipv4=0x7f000001, port=18883)
        import time
        time.sleep(.02)
        self.assertTrue(self.status()["faulted"])
        self.assertEqual(self.client.status_fields()["mode"], "wasm")

    def test_stale_speed_update_uses_core_validation(self):
        self.load('(module (import "lightvm.experimental.v1" "speed_set" (func $set (param f64 i32 i32) (result i32))) (memory 1 1) (func (export "service_init") (param i32 i32)) (func (export "service_step") (result i32) f64.const 0.4 i32.const 0 i32.const 0 call $set))')
        self.client.manage("service_start", ipv4=0x7f000001, port=18883)
        import time
        time.sleep(.02)
        self.assertGreater(self.status()["commands_rejected"], 0)
        self.assertEqual(self.client.program_parameters()["values"]["speed"], .1)

    def test_invalid_handles_and_wrapping_buffer_bounds(self):
        self.load('(module (import "lightvm.experimental.v1" "tcp_state" (func $state (param i32) (result i32))) (memory 1 1) (func (export "service_init") (param i32 i32)) (func (export "service_step") (result i32) i32.const 2147483647 call $state))')
        self.client.manage("service_start", ipv4=0x7f000001, port=18883)
        import time
        time.sleep(.01)
        self.assertEqual(self.status()["guest_state"], 0xffffffff)
        self.load('(module (import "lightvm.experimental.v1" "tcp_read" (func $read (param i32 i32 i32) (result i32))) (memory 1 1) (func (export "service_init") (param i32 i32)) (func (export "service_step") (result i32) i32.const 1 i32.const -1 i32.const 2 call $read))')
        self.client.manage("service_start", ipv4=0x7f000001, port=18883)
        time.sleep(.01)
        self.assertTrue(self.status()["faulted"])

    def test_fault_closes_native_connection(self):
        import socket
        import time
        listener=socket.socket()
        self.addCleanup(listener.close)
        listener.bind(("127.0.0.1",0)); listener.listen(1); listener.settimeout(2)
        port=listener.getsockname()[1]
        self.load('(module (import "lightvm.experimental.v1" "tcp_connect" (func $connect (param i32 i32) (result i32))) (import "lightvm.experimental.v1" "tcp_state" (func $state (param i32) (result i32))) (memory 1 1) (global $handle (mut i32) (i32.const 0)) (func (export "service_init") (param i32 i32) local.get 0 local.get 1 call $connect global.set $handle) (func (export "service_step") (result i32) global.get $handle call $state i32.const 1 i32.eq if unreachable end i32.const 0))')
        self.client.manage("service_start",ipv4=0x7f000001,port=port)
        connection,_=listener.accept()
        self.addCleanup(connection.close)
        connection.settimeout(2)
        self.assertEqual(connection.recv(1),b"")
        self.assertTrue(self.status()["faulted"])
        self.assertEqual(self.client.status_fields()["mode"],"wasm")


class HttpReplacementTests(unittest.TestCase):
    def test_independent_service_replacement_over_http(self):
        import re
        import subprocess
        executable=Path(os.environ.get("LIGHTVM_RECEIVER",ROOT/"build/mqtt-host/lightvm"))
        if not executable.exists():self.skipTest("build experimental host first")
        probe=Client(ProcessTransport(executable))
        try:
            capabilities=probe.describe()
            if not capabilities.get("mqtt_experiment") or not capabilities["http_supported"]:self.skipTest("experiment/HTTP disabled")
        finally:probe.close()
        process=subprocess.Popen([str(executable),"--http","0"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=0)
        try:
            line=process.stderr.readline().decode()
            self.assertIn("HTTP listening",line)
            client=Client(HttpTransport(re.search(r'http://\S+',line)[0]))
            try:
                client.configure({"schema":"lightvm.config/1","devices":[{"id":"strip","type":"ws2812b","config":{"pin":"GP16","count":100}}],"targets":{"A":{"device":"strip"}}})
                client.upload(compile_source((ROOT/"samples/python/rainbow.py").read_text()).wasm)
                client.require_ok(client.command(PLAY))
                generation=client.program_parameters()["generation"]
                for result in (1,2,3):
                    module=bytes(wasmtime.wat2wasm(f'(module (memory 1 1) (func (export "service_init") (param i32 i32)) (func (export "service_step") (result i32) i32.const {result}))'))
                    client.upload_object("service",module); client.manage("service_load"); client.manage("service_start",ipv4=0x7f000001,port=18883)
                    client.manage("service_status"); self.assertTrue(json.loads(client.data_reply)["running"])
                    self.assertEqual(client.program_parameters()["generation"],generation)
                self.assertEqual(client.status_fields()["mode"],"wasm")
                client.manage("service_unload")
            finally:client.close()
        finally:
            process.terminate(); process.wait(timeout=5)
            for stream in (process.stdin,process.stdout,process.stderr):stream.close()


if __name__ == "__main__":
    unittest.main()
