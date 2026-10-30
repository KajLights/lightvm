"""Named Python transitions use the real store, compiler, VM and presenter."""
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from lightvm.compiler import compile_source
from client import Client, ProcessTransport, ProtocolError, FRAME, STOP


class TransitionProfileTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.storage = Path(self.directory.name) / 'flash.bin'
        self.receiver = os.environ.get('LIGHTVM_RECEIVER', str(ROOT / 'build/ab-host/lightvm'))
        self.connect()
        self.count = 67
        self.client.configure({'schema': 'lightvm.config', 'outputs': [
            {'id': 'lights', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': self.count}}]})
        for name, color in (('a', '200,100,20'), ('b', '20,40,220')):
            self.save(name, 'from lightvm import profile,render,rgb\nprofile("animation")\n'
                      '@render\ndef draw(ctx,runtime,pixels):\n pixels.fill(rgb(' + color + '))\n')
        for name in ('crossfade', 'wipe'):
            self.save(name, (ROOT / 'examples/transitions' / (name + '.py')).read_text())
        self.compositions()

    def connect(self):
        self.client = Client(ProcessTransport(self.receiver, self.storage))

    def tearDown(self):
        self.client.close()
        self.directory.cleanup()

    def save(self, name, source):
        result = compile_source(source)
        self.assertTrue(result.success, result.diagnostics)
        self.client.save_program(name, result.bytecode)
        return result

    def compositions(self):
        for slot, name in (('A', 'a'), ('B', 'b')):
            self.client.configure_composition([{'id': 'base', 'program': name}], output='lights', slot=slot)
        self.client.require_ok(self.client.command(STOP, output='lights'))

    def frame(self, progress):
        self.client.set_progress(progress, output='lights')
        self.client.require_ok(self.client.command(FRAME, bytes(4), output='lights'))
        return bytes(self.client.pixel_snapshot)

    def test_libraries_compile_to_bounded_bulk_operations(self):
        for name, bindings in (('crossfade', {53}), ('wipe', {54})):
            result = compile_source('from lightvm import profile,render\n'
                                    'from lightvm.transitions import ' + name + '\n'
                                    'profile("transition")\n@render\ndef draw(ctx,runtime,pixels):\n '
                                    + name + '(pixels,runtime.progress)\n')
            self.assertTrue(result.success, result.diagnostics)
            self.assertEqual(result.manifest['schema'], 'lightvm.program')
            self.assertEqual(result.manifest['profile'], 'transition')
            regs, code, colors, functions, resources, expressions, calls = struct.unpack_from('<7I', result.bytecode, 8)
            at = 96 + regs*8 + code*8 + colors*4 + functions*24 + resources*24 + expressions*16
            self.assertEqual({struct.unpack_from('<I', result.bytecode, at+i*16)[0] for i in range(calls)}, bindings)
            self.assertLess(code, 40)
            self.assertLess(len(result.bytecode), 1024)
            self.assertEqual(struct.unpack_from('<I', result.bytecode, 72)[0], 4)

    def test_crossfade_exact_endpoints_complementary_weights_and_no_white_paint(self):
        state = self.client.select_transition('crossfade', output='lights')
        self.assertEqual(state['program'], 'crossfade')
        self.assertEqual(state['effect'], 'program')
        self.assertEqual((state['mask'], state['paint']), ('', ''))
        for p, rgb in ((0, (200,100,20)), (35, (137,79,90)), (100, (20,40,220)), (35, (137,79,90))):
            self.assertEqual(self.frame(p), bytes(rgb) * self.count)
        self.client.configure_composition([{'id':'base','program':'a'}], output='lights', slot='B')
        for p in (0.001, 1, 35, 49.999, 50, 99.999):
            self.assertEqual(self.frame(p), bytes((200,100,20)) * self.count)

    def test_wipe_direction_settings_and_seek_at_partial_chunks(self):
        for reverse in (False, True):
            self.client.select_transition('wipe', settings={'reverse': reverse}, output='lights')
            for p in (0, 35, 100, 75, 35, 0):
                n = int(p / 100 * self.count)
                a, b = bytes((200,100,20)), bytes((20,40,220))
                expected = a*(self.count-n)+b*n if reverse else b*n+a*(self.count-n)
                self.assertEqual(self.frame(p), expected)

    def test_wipe_separator_sprite_tracks_boundary_clips_and_seeks(self):
        for count in (1, 2, 67):
            self.count = count
            self.client.configure({'schema': 'lightvm.config', 'outputs': [
                {'id': 'lights', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': count}}]})
            self.compositions()
            for reverse in (False, True):
                self.client.select_transition('wipe', settings={'reverse': reverse, 'separator': True}, output='lights')
                heap = self.client.status_fields(output='lights')['heap']
                for p in (0, 1, 35, 50, 75, 99, 100, 50, 35, 0):
                    n = int(p / 100 * count)
                    a, b = (200, 100, 20), (20, 40, 220)
                    expected = [a] * (count-n) + [b] * n if reverse else [b] * n + [a] * (count-n)
                    if 0 < p < 100:
                        boundary = count-n if reverse else n
                        for offset, intensity in ((-1, 85), (0, 255), (1, 85)):
                            if 0 <= boundary+offset < count:
                                expected[boundary+offset] = (intensity,) * 3
                    self.assertEqual(self.frame(p), bytes(c for pixel in expected for c in pixel))
                self.assertEqual(self.client.status_fields(output='lights')['heap'], heap)

    def test_separator_compilation_omits_immutable_default_style_reads(self):
        source=(ROOT/'examples/transitions/wipe.py').read_text()
        reads=[]
        for optimized in (False,True):
            result=compile_source(source,optimize=optimized)
            self.assertTrue(result.success,result.diagnostics)
            regs,code,colors,functions,resources,expressions,calls=struct.unpack_from('<7I',result.bytecode,8)
            at=96+regs*8+code*8+colors*4+functions*24+resources*24+expressions*16
            reads.append(sum(struct.unpack_from('<I',result.bytecode,at+i*16)[0] in (40,41) for i in range(calls)))
        self.assertGreaterEqual(reads[0]-reads[1],5)

    def test_wipe_separator_colour_setting_preserves_endpoints_and_seeking(self):
        for reverse in (False, True):
            self.client.select_transition('wipe', settings={'reverse': reverse, 'separator': True, 'color': 0xff6600}, output='lights')
            for p in (0, 35, 75, 100, 35):
                n = int(p / 100 * self.count)
                a, b = (200, 100, 20), (20, 40, 220)
                expected = [a] * (self.count-n) + [b] * n if reverse else [b] * n + [a] * (self.count-n)
                if 0 < p < 100:
                    boundary = self.count-n if reverse else n
                    for offset, color in ((-1, (85, 34, 0)), (0, (255, 102, 0)), (1, (85, 34, 0))):
                        if 0 <= boundary+offset < self.count:
                            expected[boundary+offset] = color
                self.assertEqual(self.frame(p), bytes(c for pixel in expected for c in pixel))

    def test_transition_sprite_callbacks_have_no_animation_clock(self):
        for prop in ('time', 'delta'):
            result = compile_source('from lightvm import profile,init,render,Sprite,SpriteBatch,rgb\n'
                                    'profile("transition")\ndef sprite_draw(s,runtime,pixels):\n'
                                    ' pixels.fill(rgb(int(runtime.' + prop + '),0,0))\n'
                                    'Mark=Sprite(width=1,render=sprite_draw)\n'
                                    '@init\ndef setup(ctx,runtime):\n ctx.marks=SpriteBatch(Mark,capacity=1)\n ctx.marks.spawn()\n'
                                    '@render\ndef draw(ctx,runtime,pixels):\n ctx.marks.render(runtime,pixels)\n')
            self.assertFalse(result.success)
            self.assertIn('unknown context field or caller argument property', str(result.diagnostics))

    def test_opaque_layers_and_crossfades_have_identical_pixels(self):
        self.client.configure({'schema': 'lightvm.config', 'outputs': [
            {'id':name,'type':'ws2812b','config':{'pin':pin,'count':self.count}}
            for name,pin in (('lights','GP16'),('layers','GP17'))]})
        self.client.configure_composition([{'id':'lower','program':'a'},{'id':'upper','program':'b'}],output='layers')
        self.client.require_ok(self.client.command(STOP,output='layers'))
        self.client.select_transition('crossfade',output='lights')
        for opacity in (0,1,17,35,63,127,128,129,192,254,255):
            self.client.layer_opacity('upper',opacity,output='layers')
            self.client.require_ok(self.client.command(FRAME,bytes(4),output='layers'))
            expected=bytes(self.client.pixel_snapshot)
            self.assertEqual(self.frame(opacity*100/255),expected)

    def test_flash_names_survive_restart_and_profiles_are_discoverable(self):
        self.client.save_config()
        before = {p['name']: p for p in self.client.programs()}
        self.assertEqual(before['crossfade']['profile'], 'transition')
        self.assertEqual(before['a']['profile'], 'animation')
        self.client.close(); self.connect(); self.compositions()
        self.assertEqual(self.client.get_program('crossfade'), compile_source(
            (ROOT / 'examples/transitions/crossfade.py').read_text()).bytecode)
        self.client.select_transition('crossfade', output='lights')
        self.assertEqual(self.frame(35), bytes((137,79,90)) * self.count)

    def test_failed_selection_and_wrong_profile_preserve_running_state(self):
        self.client.select_transition('crossfade', output='lights')
        expected = self.frame(35)
        self.client.run_transition(100, 5000, output='lights')
        state = self.client.transition(output='lights')
        for name, settings in (('missing', None), ('a', None), ('wipe', {'reverse': 1}), ('wipe', {'missing': True})):
            with self.assertRaises(ProtocolError):
                self.client.select_transition(name, settings=settings, output='lights')
            after = self.client.transition(output='lights')
            self.assertEqual(after['program'], 'crossfade')
            self.assertEqual(after['revision'], state['revision'])
            self.assertEqual(after['driver']['assignment'], state['driver']['assignment'])
        with self.assertRaises(ProtocolError): self.client.activate('wipe', output='lights')
        with self.assertRaises(ProtocolError): self.client.configure_composition([{'id':'bad','program':'wipe'}], output='lights')
        with self.assertRaises(ProtocolError): self.client.delete_program('crossfade')
        self.assertEqual(self.frame(35), expected)

    def test_evaluation_uses_progress_and_restores_state_before_each_seek(self):
        self.save('seek', '''from lightvm import profile,init,render,rgb
profile("transition")
@init
def setup(ctx,runtime):
 ctx.calls=0
@render
def draw(ctx,runtime,pixels):
 ctx.calls+=1
 pixels.fill(rgb(ctx.calls,int(runtime.progress*100.0),0))
''')
        self.client.select_transition('seek', output='lights')
        for p in (35, 80, 35):
            self.assertEqual(self.frame(p), bytes((2,p,0)) * self.count)
        self.assertEqual(self.frame(0), bytes((200,100,20)) * self.count)
        self.assertEqual(self.frame(100), bytes((20,40,220)) * self.count)
        heap = self.client.status_fields(output='lights')['heap']
        for _ in range(10): self.frame(35)
        self.assertEqual(self.client.status_fields(output='lights')['heap'], heap)

    def test_seek_restores_large_owned_payload_across_checkpoint_chunks(self):
        self.save('seek_array', '''from array import array
from lightvm import profile,init,render,rgb
profile("transition")
@init
def setup(ctx,runtime):
 ctx.data=array("i",[0]*300)
 ctx.data[128]=7
@render
def draw(ctx,runtime,pixels):
 ctx.data[0]+=1
 ctx.data[128]+=3
 ctx.data[299]+=5
 pixels.fill(rgb(ctx.data[0],ctx.data[128],ctx.data[299]+int(runtime.progress*100.0)))
''')
        self.client.select_transition('seek_array',output='lights')
        for p in (35,80,35,1,99,35):
            self.assertEqual(self.frame(p),bytes((2,13,10+p))*self.count)

    def test_replacing_a_stored_name_preserves_the_pinned_active_version(self):
        self.client.select_transition('crossfade', output='lights')
        state = self.client.transition(output='lights')
        self.save('crossfade', (ROOT / 'examples/transitions/wipe.py').read_text())
        self.assertEqual(self.frame(35), bytes((137,79,90)) * self.count)
        self.assertEqual(self.client.transition(output='lights')['program_crc32'], state['program_crc32'])
        self.client.select_transition('crossfade', output='lights')
        self.assertNotEqual(self.client.transition(output='lights')['program_crc32'], state['program_crc32'])
        n = int(.35 * self.count)
        self.assertEqual(self.frame(35), bytes((20,40,220))*n + bytes((200,100,20))*(self.count-n))

    def test_fault_keeps_the_completed_output_and_a_fresh_selection_recovers(self):
        self.save('fault', '''from lightvm import profile,render
profile("transition")
@render
def draw(ctx,runtime,pixels):
 if runtime.progress > 0.5:
  pixels.mix(runtime.progress, stop=pixels.count+1)
 else:
  pixels.mix(runtime.progress)
''')
        self.client.select_transition('fault', output='lights')
        expected = self.frame(35)
        self.client.set_progress(75, output='lights')
        with self.assertRaises(ProtocolError): self.client.require_ok(self.client.command(FRAME, bytes(4), output='lights'))
        self.assertEqual(bytes(self.client.composition_snapshot(output='lights')[1]), expected)
        self.client.select_transition('crossfade', output='lights')
        self.assertEqual(self.frame(35), expected)

    def test_progress_and_composition_access_are_profile_checked(self):
        source = (ROOT / 'examples/transitions/crossfade.py').read_text()
        self.assertFalse(compile_source(source, target={'version':'0.1.0'}).success)
        self.assertTrue(compile_source(source, target={'version':'0.1.0', 'transition_programs':True}).success)
        for source in (
            'profile("animation")\n@render\ndef draw(ctx,runtime,pixels):\n pixels.mix(0.5)\n',
            'profile("animation")\n@render\ndef draw(ctx,runtime,pixels):\n pixels.fill(rgb(int(runtime.progress),0,0))\n',
            'profile("transition")\n@render\ndef draw(ctx,runtime,pixels):\n pixels.fill(rgb(int(runtime.time),0,0))\n',
            'profile("transition")\n@init\ndef setup(ctx,runtime):\n pixels.mix(0.5)\n@render\ndef draw(ctx,runtime,pixels):\n pixels.mix(runtime.progress)\n',
        ):
            result = compile_source('from lightvm import profile,init,render,rgb\n' + source)
            self.assertFalse(result.success)


if __name__ == '__main__': unittest.main()
