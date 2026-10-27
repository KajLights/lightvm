"""Same LVAF records through real UDP and framed gateway ingress.

The small independent LVAS peer below is a deterministic simulated publisher,
not a second physical Sense. No microphone or speakers are involved.
"""
import json
import os
from pathlib import Path
import socket
import struct
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))

from bytecode_fixtures import ROOT, program
from client import Client, ProcessTransport, ProtocolError, FRAME
from test_multi_output import configuration

CONTROL = struct.Struct('!4sBBBB8QHBBI')
SOURCE = 0x9000000000000007  # Persistent non-MAC identity, above JS integer range.
SESSION = 0xf000000000000008
EVENT = 0xf100000000000001
METRICS = ('level', 'bass', 'mid', 'treble', 'centroid', 'onset')
EDGES = [2 + n * 510 // 64 for n in range(65)]
CODE = '''from lightvm import profile,render,Input,rgb
profile("animation")
U=Input("udp",default=0.125,minimum=0.0,maximum=1.0)
G=Input("gateway",default=0.25,minimum=0.0,maximum=1.0)
UE=Input("udp_events",default=0)
GE=Input("gateway_events",default=0)
@render
def draw(ctx,runtime,pixels):
 pixels.fill(rgb(int(runtime.input(U)*128.0),int(runtime.input(G)*128.0),(runtime.input(UE)+runtime.input(GE))%256))
'''
BINDINGS = {'udp': {'source': 'audio.level'}, 'gateway': {'source': 'desk.level'},
            'udp_events': {'source': 'audio.onsets'}, 'gateway_events': {'source': 'desk.onsets'}}


def now_us():
    return time.monotonic_ns() // 1000


def record(sequence, *, level=0.75, events=(), source=SOURCE, session=SESSION):
    """All 70 values differ, are exactly representable, and retain uint64 IDs."""
    capture = now_us()
    values = (level, 0.25, 0.375, 0.5, 0.625, 0.875)
    data = struct.pack('>4s4B6QIHH6f4B', b'LVAF', 5, 6, len(events), 0,
                       source, session, sequence, capture, capture, 1,
                       48000, 1024, 2048, *values, 64, 1, 0, 0)
    data += b''.join(struct.pack('>HHf', EDGES[n], EDGES[n+1], (n+1)/128) for n in range(64))
    data += b''.join(struct.pack('>QQB3xf', identity, capture if stamp is None else stamp, 1, 0.5)
                     for identity, stamp in events)
    return data


class Publisher:
    """Bounded test peer: OPEN/OFFER/KEEP/ACK/CLOSE with explicit restart."""
    def __init__(self, source=SOURCE, session=SESSION):
        self.source, self.session = source, session
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))
        self.socket.setblocking(False)
        self.port = self.socket.getsockname()[1]
        self.peers = {}
        self.tokens = 100
        self.opens = self.keeps = self.closes = 0
        self.drop_open = False

    def configuration(self):
        return {'type': 'lvaf', 'address': '127.0.0.1', 'port': self.port,
                'source_id': f'{self.source:016x}', 'analysis_profile': 1}

    def poll(self):
        for _ in range(32):
            try:
                data, address = self.socket.recvfrom(701)
            except BlockingIOError:
                break
            magic, version, kind, status, reserved, client, request, lease, pin, session, stamp, seq, event, ms, bands, wire, profile = CONTROL.unpack(data)
            assert (magic, version, status, reserved, session, stamp, event, ms, bands, wire, profile) == (b'LVAS', 1, 0, 0, 0, 0, 0, 0, 0, 5, 1)
            assert pin in (0, self.source)
            key = (address, client)
            if kind == 1:
                self.opens += 1
                if self.drop_open:
                    self.drop_open = False
                    continue
                if key not in self.peers:
                    self.tokens += 1
                    self.peers[key] = [self.tokens, False]
                lease = self.peers[key][0]
                reply, status, ms = 2, 0, 2000
            elif kind == 3:
                self.keeps += 1
                if key not in self.peers or lease != self.peers[key][0]:
                    reply, status, ms = 6, 2, 0
                else:
                    self.peers[key][1] = True
                    reply, status, ms = 4, 0, 15000
            elif kind == 5:
                self.closes += 1
                self.peers.pop(key, None)
                continue
            else:
                raise AssertionError(f'unexpected control {kind}')
            self.socket.sendto(CONTROL.pack(b'LVAS', 1, reply, status, 0, client, request,
                lease, self.source, self.session if not status else 0, now_us() if not status else 0,
                0, 0, ms, 64 if not status else 0, 5, 1), address)

    def send(self, data):
        for (address, _), (token, active) in self.peers.items():
            if active:
                packet = bytearray(data)
                struct.pack_into('>Q', packet, 48, token)
                self.socket.sendto(packet, address)

    def restart(self):
        self.session += 1
        self.peers.clear()

    def close(self):
        self.socket.close()


class TransportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.client = Client(ProcessTransport(Path(os.environ.get('LIGHTVM_RECEIVER', ROOT/'build/host/lightvm')),
                                               Path(self.temp.name)/'store.bin'))
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(self.client.close)
        self.client.configure(configuration(2, 5))
        self.client.save_program('mixed', program(CODE))
        self.publishers = []
        self.publisher = self.new_publisher()
        self.frame_id = 0

    def new_publisher(self, **fields):
        publisher = Publisher(**fields)
        self.publishers.append(publisher)
        self.addCleanup(publisher.close)
        return publisher

    def providers(self):
        self.client.manage('audio_status')
        return json.loads(self.client.data_reply)['providers']

    def until(self, predicate, timeout=2):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for publisher in self.publishers:
                publisher.poll()
            if predicate():
                return
            time.sleep(0.002)
        self.fail('timed out: ' + json.dumps(self.providers()))

    def activate(self, output='lights0', slot=None, publisher=None, gateway='desk'):
        publisher = publisher or self.publisher
        self.client.activate('mixed', output=output, slot=slot, inputs=BINDINGS, sources={
            'audio': publisher.configuration(), 'desk': {'type': 'gateway', 'name': gateway}})

    def acquire(self):
        self.until(lambda: any(p['type'] == 'lvaf' and p['session'] == f'{self.publisher.session:016x}'
                               for p in self.providers()))
        self.claim = self.client.claim_audio('desk', f'{SOURCE:016x}', f'{self.publisher.session:016x}')

    def gateway(self, data, claim=None):
        self.frame_id += 1
        self.client.publish_audio(claim or self.claim, self.frame_id, data)

    def both(self, data):
        self.publisher.send(data)
        self.gateway(data)

    def inputs(self, output='lights0', slot=None):
        self.client.require_ok(self.client.command(FRAME, bytes(4), output=output))
        state = self.client.program_state(output=output, slot=slot)
        return {key: value['value'] for key, value in state['inputs'].items()}

    def expect(self, udp, gateway, udp_events, gateway_events, **where):
        expected = dict(udp=udp, gateway=gateway, udp_events=udp_events, gateway_events=gateway_events)
        self.until(lambda: self.inputs(**where) == expected)

    def test_identical_values_events_loss_reorder_delay_and_reconnect(self):
        self.publisher.drop_open = True  # Lost initial control retries without a second lease.
        self.activate()
        self.acquire()
        self.assertGreaterEqual(self.publisher.opens, 2)
        first = record(1, level=0.5, events=[(EVENT+n, None) for n in range(4)])
        self.assertEqual(len(first), 700)
        self.both(first)
        self.expect(0.5, 0.5, 4, 4)
        self.both(first)  # Independent outer gateway frame ID, same LVAF identity.
        self.expect(0.5, 0.5, 4, 4)
        older = record(2, level=0.25, events=[(EVENT+4, None)])
        time.sleep(0.001)
        newer = record(4, level=0.75, events=[(EVENT+5, None)])  # Sequence 3 is lost.
        self.both(newer)
        self.expect(0.75, 0.75, 5, 5)
        self.both(older)  # Recover unseen lower event ID without rewinding metrics.
        self.expect(0.75, 0.75, 6, 6)
        self.both(older)
        self.expect(0.75, 0.75, 6, 6)
        for provider in self.providers():
            self.assertEqual(provider['sequence'], '0000000000000004')
            self.assertEqual(provider['band_edges'], EDGES)
            self.assertEqual(provider['source_id'], f'{SOURCE:016x}')

        # Compare all six metrics and all 64 distinct bands through actual INPUT
        # capture, avoiding any test-only receiver introspection or RGB rounding.
        for index, name in enumerate((*METRICS, *(f'band{n:02}' for n in range(64)))):
            self.client.set_inputs({'udp': {'source': 'audio.'+name}, 'gateway': {'source': 'desk.'+name}}, output='lights0')
            packet = record(5+index)
            self.both(packet)
            value = (0.75, 0.25, 0.375, 0.5, 0.625, 0.875)[index] if index < 6 else (index-5)/128
            self.expect(value, value, 6, 6)
        self.client.set_inputs({'udp': BINDINGS['udp'], 'gateway': BINDINGS['gateway']}, output='lights0')

        delayed = record(75, events=[(EVENT+6, None)])
        time.sleep(0.38)
        self.publisher.send(delayed)
        with self.assertRaisesRegex(ProtocolError, 'stale'):
            self.gateway(delayed)
        self.expect(0.125, 0.25, 6, 6)
        self.assertTrue(all(p['age_rejections'] for p in self.providers()))
        # A fresh packet cannot give an expired occurrence a new timestamp.
        fresh = record(76, events=[(EVENT+6, now_us()-300000)])
        time.sleep(0.08)  # Metric age < 350 ms, occurrence age > 350 ms.
        self.both(fresh)
        self.expect(0.75, 0.75, 6, 6)

        self.publisher.restart()  # Process restart: same persistent source and port.
        self.until(lambda: any(p['type'] == 'lvaf' and p['session'] == f'{self.publisher.session:016x}'
                               for p in self.providers()), timeout=3)
        old_claim = self.claim
        self.claim = self.client.claim_audio('desk', f'{SOURCE:016x}', f'{self.publisher.session:016x}')
        with self.assertRaises(ProtocolError):
            self.gateway(fresh, old_claim)
        self.publisher.send(fresh)  # Old session never becomes current again.
        with self.assertRaises(ProtocolError):
            self.gateway(fresh)
        self.expect(0.125, 0.25, 6, 6)
        restarted = record(1, session=self.publisher.session, events=[(1, None)])
        self.both(restarted)
        self.expect(0.75, 0.75, 7, 7)
        self.both(restarted)
        self.expect(0.75, 0.75, 7, 7)

    def test_mixed_crossfade_scoped_aliases_shared_outputs_and_independent_timeout(self):
        other = self.new_publisher(source=SOURCE+1, session=SESSION+10)
        self.activate()
        self.activate(output='lights1')  # Same canonical UDP endpoint/one lease.
        self.activate(slot='B', publisher=other, gateway='other')
        self.client.set_progress(50, output='lights0')
        self.acquire()
        self.until(lambda: any(p['port'] == other.port and p['session'] == f'{other.session:016x}'
                               for p in self.providers()))
        other_claim = self.client.claim_audio('other', f'{other.source:016x}', f'{other.session:016x}')
        self.assertEqual(len(self.providers()), 4)
        self.assertEqual(len(self.publisher.peers), 1)
        self.assertEqual(len(other.peers), 1)
        packet = record(1, level=0.5)
        different = record(1, level=0.875, source=other.source, session=other.session)
        self.both(packet)
        other.send(different)
        self.gateway(different, other_claim)
        self.expect(0.5, 0.5, 0, 0, slot='A')
        self.expect(0.875, 0.875, 0, 0, slot='B')
        self.expect(0.5, 0.5, 0, 0, output='lights1')
        self.assertEqual(bytes(self.client.pixel_snapshot[:2]), bytes([64, 64]))
        self.client.command(FRAME, bytes(4), output='lights0')
        self.assertEqual(bytes(self.client.pixel_snapshot[:2]), bytes([88, 88]))
        generic = self.client.claim_inputs({'audio.level': 'float', 'desk.level': 'float'})
        self.client.publish_inputs(generic, 1, {'audio.level': 0.0, 'desk.level': 0.0})
        self.expect(0.5, 0.5, 0, 0, slot='A')
        self.expect(0.875, 0.875, 0, 0, slot='B')

        before = self.client.composition(output='lights0', slot='B')
        heap = self.client.status_fields()['heap']
        with self.assertRaisesRegex(ProtocolError, 'capacity exhausted'):
            self.client.configure_composition([{'id': 'main', 'program': 'mixed', 'inputs': BINDINGS}],
                output='lights0', slot='B', sources={'audio': other.configuration(),
                    'desk': {'type': 'gateway', 'name': 'fifth'}})
        after = self.client.composition(output='lights0', slot='B')
        for key in ('revision', 'active', 'preset', 'sources', 'playing', 'error'):
            self.assertEqual(after[key], before[key])
        for key in ('id', 'program', 'generation', 'playing', 'opacity', 'blend', 'faulted'):
            self.assertEqual(after['layers'][0][key], before['layers'][0][key])
        self.assertEqual(self.client.status_fields()['heap'], heap)
        self.assertEqual(len(self.providers()), 4)

        time.sleep(0.38)
        self.gateway(record(2, level=0.625))
        self.expect(0.125, 0.625, 0, 0, slot='A')
        self.expect(0.125, 0.25, 0, 0, slot='B')
        self.expect(0.125, 0.625, 0, 0, output='lights1')
        self.publisher.send(record(2, level=0.375))
        self.expect(0.375, 0.625, 0, 0, slot='A')
        self.client.release_audio(self.claim)
        self.expect(0.375, 0.25, 0, 0, slot='A')
        self.claim = self.client.claim_audio('desk', f'{SOURCE:016x}', f'{SESSION+1:016x}')
        self.gateway(record(1, level=0.5, session=SESSION+1))
        self.expect(0.375, 0.5, 0, 0, slot='A')
        self.assertEqual(len(self.publisher.peers), 1)
        self.assertEqual(self.publisher.opens, 1)


if __name__ == '__main__':
    unittest.main()
