"""Structured audio through the real host engine and framed management protocol."""
import json
import os
from pathlib import Path
import struct
import tempfile
import time
import unittest

from bytecode_fixtures import ROOT, program
from client import Client, ProcessTransport, ProtocolError, FRAME, PLAY, STOP
from test_configuration import config

SOURCE = '''from lightvm import profile,render,Input,rgb
profile("animation")
LEVEL=Input("level",default=0.25,minimum=0.0,maximum=1.0)
BAND=Input("band",default=0.0,minimum=0.0,maximum=1.0)
EVENTS=Input("events",default=0)
@render
def draw(ctx,runtime,pixels):
 pixels.fill(rgb(int(runtime.input(LEVEL)*100.0),int(runtime.input(BAND)*100.0),runtime.input(EVENTS)%256))
'''
SOURCES = {'audio': {'type': 'gateway', 'name': 'desk', 'analysis_profile': 1, 'allow_synthetic': False}}
BINDINGS = {'level': {'source': 'audio.level'}, 'band': {'source': 'audio.band63'}, 'events': {'source': 'audio.onsets'}}
SOURCE_ID = 0x9000000000000007
SESSION_ID = 0xf000000000000008


def record(sequence, *, level=0.75, events=(), capture=None, source=SOURCE_ID, session=SESSION_ID):
    """Independent wire fixture, including every band and uint64 values > 2**53."""
    now = time.monotonic_ns() // 1000
    capture = now if capture is None else capture
    data = bytearray(92 + 64 * 8 + len(events) * 24)
    struct.pack_into('>4s4B6QIHH6f4B', data, 0, b'LVAF', 5, 6, len(events), 0,
                     source, session, sequence, capture, now, 1, 48000, 1024, 2048,
                     level, 0.2, 0.3, 0.4, 0.5, 0.6, 64, 1, 0, 0)
    for band in range(64):
        struct.pack_into('>HHf', data, 92 + band * 8, 2 + band * 510 // 64,
                         2 + (band + 1) * 510 // 64, 0.5)
    for index, (identity, timestamp) in enumerate(events):
        struct.pack_into('>QQB3xf', data, 604 + index * 24, identity, timestamp, 1, 0.5)
    return bytes(data)


class GatewayTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.receiver = Path(os.environ.get('LIGHTVM_RECEIVER', ROOT / 'build/host/lightvm'))
        self.client = Client(ProcessTransport(self.receiver, Path(self.temp.name) / 'store.bin'))
        self.client.configure(config(5))
        self.code = program(SOURCE)
        self.client.save_program('reactive', self.code)

    def tearDown(self):
        self.client.close()
        self.temp.cleanup()

    def providers(self):
        self.client.manage('audio_status')
        return json.loads(self.client.data_reply)['providers']

    def claim(self, name='desk', profile=1):
        return self.client.claim_audio(name, f'{SOURCE_ID:016x}', f'{SESSION_ID:016x}', profile)

    def frame(self):
        self.client.require_ok(self.client.command(FRAME, struct.pack('<I', 0)))
        return bytes(self.client.pixel_snapshot[:3])

    def test_gateway_drives_scoped_inputs_and_does_not_borrow_upload(self):
        self.client.activate('reactive', inputs=BINDINGS, sources=SOURCES)
        self.assertEqual(self.frame(), bytes([25, 0, 0]))
        claim = self.claim()
        timestamp = time.monotonic_ns() // 1000
        events = [(0xf100000000000001 + n, timestamp) for n in range(4)]
        self.client.publish_audio(claim, 1, record(1, events=events))
        self.assertEqual(self.frame(), bytes([75, 50, 4]))
        status = self.providers()[0]
        self.assertEqual((status['type'], status['port'], status['expected_source_id'], status['expected_profile'], status['allow_synthetic']),
                         ('gateway', 0, '0000000000000000', 1, False))
        self.assertEqual((status['source_id'], status['profile'], status['bands']), (f'{SOURCE_ID:016x}', 1, 64))
        self.client.publish_audio(claim, 2, record(2, events=events))
        self.assertEqual(self.frame(), bytes([75, 50, 4]))
        # Generic scalar ownership cannot write or revoke the scoped stream.
        generic = self.client.claim_inputs({'audio.level': 'float'})
        self.client.publish_inputs(generic, 1, {'audio.level': 0.1})
        self.assertEqual(self.frame(), bytes([75, 50, 4]))
        # Program/config staging remains independent of the 700-byte frame.
        self.client.upload_object('config', json.dumps(config(6)).encode())
        self.client.publish_audio(claim, 3, record(3))
        self.client.manage('config_validate')
        self.assertEqual(self.frame(), bytes([75, 50, 4]))
        self.client.release_audio(claim)
        self.assertEqual(self.frame(), bytes([25, 0, 4]))
        self.assertEqual(self.client.program_state()['sources'], SOURCES)

    def test_scope_persistence_pause_and_generation_replacement(self):
        layers = [{'id': 'main', 'program': 'reactive', 'crc32': __import__('zlib').crc32(self.code), 'inputs': BINDINGS}]
        self.client.save_preset('desk', layers, sources=SOURCES)
        self.assertEqual(self.providers(), [])  # Saving validates; it never acquires.
        self.assertEqual(self.client.preset('desk')['sources'], SOURCES)
        self.client.activate_preset('desk')
        self.assertEqual(self.client.composition()['sources'], SOURCES)
        self.assertEqual(self.client.program_state(layer='main')['sources'], SOURCES)
        first = self.claim()
        self.client.publish_audio(first, 1, record(1))
        self.assertEqual(self.frame(), bytes([75, 50, 0]))
        self.client.pause_program(layer='main')
        self.assertEqual(self.providers()[0]['references'], 0)
        self.client.publish_audio(first, 2, record(2, events=[(1, time.monotonic_ns() // 1000)]))
        self.client.resume_program(layer='main')
        self.assertEqual(self.frame(), bytes([75, 50, 0]))  # No paused occurrence replay.
        second = self.claim()
        self.assertNotEqual(first['generation'], second['generation'])
        with self.assertRaises(ProtocolError): self.client.publish_audio(first, 3, record(3))
        with self.assertRaises(ProtocolError): self.client.release_audio(first)
        self.client.publish_audio(second, 1, record(1, events=[(2, time.monotonic_ns() // 1000)]))
        self.assertEqual(self.frame(), bytes([75, 50, 1]))
        state = self.client.reset_program(layer='main')
        self.assertEqual(state['sources'], SOURCES)
        self.assertEqual(self.frame()[2], 0)
        self.client.clear_composition()
        self.assertEqual(self.providers()[0]['references'], 0)
        self.client.release_audio(second)
        self.assertEqual(self.providers(), [])

    def test_wrong_profile_boot_and_stale_capture_stay_unavailable(self):
        self.client.activate('reactive', inputs=BINDINGS, sources=SOURCES)
        claim = self.claim(profile=2)
        self.client.publish_audio(claim, 1, record(1))
        self.assertEqual(self.frame(), bytes([25, 0, 0]))
        with self.assertRaises(ProtocolError):
            self.client.manage('audio_keep', boot_id='old-boot', generation=claim['generation'])
        claim = self.claim()
        with self.assertRaises(ProtocolError): self.client.publish_audio(claim, 1, record(1, capture=time.monotonic_ns() // 1000 - 400000))
        with self.assertRaises(ProtocolError): self.client.publish_audio(claim, 2, record(2, source=9))
        self.assertEqual(self.frame(), bytes([25, 0, 0]))
        self.client.publish_audio(claim, 3, record(3))
        self.assertEqual(self.frame(), bytes([75, 50, 0]))

    def test_direct_resume_rejects_full_pool_before_changing_playback(self):
        sources = {'audio': {'type': 'gateway', 'name': 'desk'}, 'second': {'type': 'gateway', 'name': 'room'}}
        self.client.activate('reactive', inputs=BINDINGS, sources=sources)
        self.client.pause_program()
        claims = [self.claim('held' + str(n)) for n in range(3)]
        before = self.client.program_state()
        self.assertTrue(before['paused'])
        for resume in (self.client.resume_program, lambda: self.client.require_ok(self.client.command(PLAY))):
            with self.assertRaisesRegex(ProtocolError, 'capacity exhausted'):
                resume()
            after = self.client.program_state()
            self.assertTrue(after['paused'])
            self.assertEqual(after['generation'], before['generation'])
            self.assertEqual(after['inputs'], before['inputs'])
            self.assertEqual(len(self.providers()), 3)
            self.assertTrue(all(provider['references'] == 0 for provider in self.providers()))
        self.client.release_audio(claims.pop())
        self.client.resume_program()
        self.assertFalse(self.client.program_state()['paused'])
        self.assertEqual(len(self.providers()), 4)
        self.assertEqual(sum(provider['references'] for provider in self.providers()), 2)

    def test_composition_resume_preserves_revision_on_capacity_failure(self):
        layers = [{'id': 'main', 'program': 'reactive', 'crc32': __import__('zlib').crc32(self.code), 'inputs': BINDINGS}]
        sources = {'audio': {'type': 'gateway', 'name': 'desk'}, 'second': {'type': 'gateway', 'name': 'room'}}
        self.client.configure_composition(layers, sources=sources)
        self.client.pause_composition()
        claims = [self.claim('held' + str(n)) for n in range(3)]
        before = self.client.composition()
        with self.assertRaisesRegex(ProtocolError, 'capacity exhausted'):
            self.client.pause_composition(paused=False)
        after = self.client.composition()
        self.assertFalse(after['playing'])
        self.assertEqual(after['revision'], before['revision'])
        self.assertEqual(len(self.providers()), 3)
        self.client.release_audio(claims.pop())
        self.client.pause_composition(paused=False)
        self.assertTrue(self.client.composition()['playing'])
        self.assertEqual(sum(provider['references'] for provider in self.providers()), 2)

    def test_ab_play_reserves_both_scopes_atomically(self):
        layers = [{'id': 'main', 'program': 'reactive', 'crc32': __import__('zlib').crc32(self.code), 'inputs': BINDINGS}]
        for slot in ('A', 'B'):
            sources = {'audio': {'type': 'gateway', 'name': slot + '1'}, 'second': {'type': 'gateway', 'name': slot + '2'}}
            self.client.configure_composition(layers, slot=slot, sources=sources)
        self.assertEqual(len(self.providers()), 4)
        self.client.require_ok(self.client.command(STOP))
        self.assertEqual(self.providers(), [])
        claim = self.claim('held')
        before = self.client.transition()
        with self.assertRaisesRegex(ProtocolError, 'capacity exhausted'):
            self.client.require_ok(self.client.command(PLAY))
        after = self.client.transition()
        self.assertFalse(after['playing'])
        self.assertEqual(after['revision'], before['revision'])
        self.assertEqual(after['progress'], before['progress'])
        self.assertEqual(len(self.providers()), 1)
        self.assertEqual(self.providers()[0]['references'], 0)
        self.client.release_audio(claim)
        self.client.require_ok(self.client.command(PLAY))
        self.assertTrue(self.client.transition()['playing'])
        self.assertEqual(len(self.providers()), 4)
        self.assertEqual(sum(provider['references'] for provider in self.providers()), 4)

    def test_layer_resume_admits_only_when_parent_runs(self):
        layers = [{'id': 'main', 'program': 'reactive', 'crc32': __import__('zlib').crc32(self.code), 'inputs': BINDINGS}]
        self.client.configure_composition(layers, sources=SOURCES)
        self.client.pause_program(layer='main')
        claims = [self.claim('held' + str(n)) for n in range(4)]
        for resume in (lambda: self.client.resume_program(layer='main'),
                       lambda: self.client.manage('output_command', layer='main', command=PLAY)):
            with self.assertRaisesRegex(ProtocolError, 'capacity exhausted'):
                resume()
            self.assertTrue(self.client.program_state(layer='main')['paused'])
            self.assertEqual(len(self.providers()), 4)
        self.client.pause_composition()
        self.client.resume_program(layer='main')  # Parent remains paused; no source demand.
        self.assertFalse(self.client.program_state(layer='main')['paused'])
        self.assertFalse(self.client.composition()['playing'])
        self.assertEqual(sum(provider['references'] for provider in self.providers()), 0)
        self.client.release_audio(claims.pop())
        self.client.pause_composition(paused=False)
        self.assertEqual(sum(provider['references'] for provider in self.providers()), 1)

    def test_slot_b_play_preserves_paused_scene_when_pool_is_full(self):
        layers = [{'id': 'main', 'program': 'reactive', 'crc32': __import__('zlib').crc32(self.code), 'inputs': BINDINGS}]
        self.client.configure_composition(layers)
        self.client.configure_composition(layers, slot='B', sources={
            'audio': {'type': 'gateway', 'name': 'desk'}, 'second': {'type': 'gateway', 'name': 'room'}})
        self.client.pause_composition(slot='B')
        claims = [self.claim('held' + str(n)) for n in range(3)]
        before = self.client.composition(slot='B')
        with self.assertRaisesRegex(ProtocolError, 'capacity exhausted'):
            self.client.require_ok(self.client.command(PLAY, slot='B'))
        after = self.client.composition(slot='B')
        self.assertFalse(after['playing'])
        self.assertEqual(after['revision'], before['revision'])
        self.assertEqual(len(self.providers()), 3)
        self.client.release_audio(claims.pop())
        self.client.require_ok(self.client.command(PLAY, slot='B'))
        self.assertTrue(self.client.composition(slot='B')['playing'])
        self.assertEqual(sum(provider['references'] for provider in self.providers()), 2)

    def test_composition_resume_reserves_hidden_retained_scene(self):
        layers = [{'id': 'main', 'program': 'reactive', 'crc32': __import__('zlib').crc32(self.code), 'inputs': BINDINGS}]
        self.client.save_preset('first', layers, sources={
            'audio': {'type': 'gateway', 'name': 'first1'}, 'second': {'type': 'gateway', 'name': 'first2'}})
        self.client.save_preset('second', layers, sources={'audio': {'type': 'gateway', 'name': 'second'}})
        self.client.recall_preset('first')
        self.client.recall_preset('second')
        self.assertEqual(len(self.providers()), 1)
        claims = [self.claim('held' + str(n)) for n in range(2)]
        before = self.client.transition()
        with self.assertRaisesRegex(ProtocolError, 'capacity exhausted'):
            self.client.pause_composition(slot='A', paused=False)
        after = self.client.transition()
        self.assertEqual(after['revision'], before['revision'])
        self.assertEqual(after['progress'], 100)
        self.assertEqual(len(self.providers()), 3)
        self.client.release_audio(claims.pop())
        self.client.pause_composition(slot='A', paused=False)
        self.assertEqual(len(self.providers()), 4)
        self.assertEqual(sum(provider['references'] for provider in self.providers()), 3)


if __name__ == '__main__':
    unittest.main()
