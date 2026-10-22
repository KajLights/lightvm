"""Permanent replay and compiler-clock regression contracts."""
import os
from pathlib import Path
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from replay_performance import measure, replay, fixed_variants
from client import Client, ProcessTransport, FRAME, STOP, PLAY
from lightvm.compiler import compile_source
import struct


class PerformanceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.executable = os.environ.get('LIGHTVM_RECEIVER', str(ROOT / 'build/multicore-host/lightvm'))

    def test_sprite_variants_and_seeded_lifetimes_match(self):
        result = measure(self.executable)
        animated = result['animated']
        self.assertEqual(animated['current']['sha256_all_frames'],
                         '7b9e75d072e4222c88432dc763acae1312938506eebe62f801c8cc87dbd0097d')
        self.assertLess(animated['current']['counters']['dispatch_count'],
                        animated['original_reference']['counters']['dispatch_count'])
        fixed = result['fixed']['12']
        self.assertLess(fixed['style_at_spawn']['counters']['dispatch_count'],
                        fixed['original']['counters']['dispatch_count'])

    def test_conditional_helper_delta_keeps_callback_history(self):
        source = fixed_variants(1)['geometry_at_spawn']
        source = source.replace('def draw_star(s,runtime,pixels):',
            'def elapsed(rt):\n return rt.delta\n'
            'def draw_star(s,runtime,pixels):')
        source = source.replace(' s.color=ColorTransform(alpha=0.75)',
            ' if runtime.time > 0.05:\n  s.color=ColorTransform(alpha=elapsed(runtime)*10.0)')
        original, reference = replay(self.executable, source, 20, optimize=False)
        optimized, images = replay(self.executable, source, 20, optimize=True)
        self.assertEqual(reference, images)
        self.assertEqual(original['sha256_all_frames'], optimized['sha256_all_frames'])

    def test_performance_advice_does_not_block_compilation(self):
        source=fixed_variants(1)['original']
        artifact=compile_source(source)
        self.assertTrue(artifact.success,artifact.diagnostics)
        self.assertTrue(any('unused sprite' in note.message for note in artifact.advisories))
        geometry=[note for note in artifact.advisories if 'geometry' in note.message]
        self.assertEqual(len(geometry),1)
        self.assertIn('Transform',source.splitlines()[geometry[0].line-1])
        self.assertFalse(any('geometry' in note.message for note in compile_source(fixed_variants(1)['geometry_at_spawn']).advisories))

    def test_map_and_resource_advice_explains_selection_at_source(self):
        source='from lightvm import profile,init,render,rgb\nprofile("animation")\n@init\ndef setup(ctx,runtime):\n ctx.x=0\n@render\ndef draw(ctx,runtime,pixels):\n for i in range(pixels.count):\n  pixels[i]=rgb(12,34,56)\n'
        for candidate,optimize,reason in ((source,True,'uses a native map'),(source,False,'disabled'),
                (source.replace('  pixels[i]', '  ctx.x=ctx.x+1\n  pixels[i]'),True,'uses the interpreter')):
            artifact=compile_source(candidate,optimize=optimize)
            self.assertTrue(artifact.success,artifact.diagnostics)
            self.assertTrue(any(note.line==8 and reason in note.message for note in artifact.advisories))
            self.assertTrue(any(note.line==7 and 'Resource estimate' in note.message and 'total RAM' in note.message for note in artifact.advisories))

    def test_time_indexed_births_match_through_expiry_and_reuse(self):
        source=(ROOT/'tests/fixtures/performance/timed-sprites.py').read_text()
        _,reference=replay(self.executable,source,650,optimize=False)
        _,optimized=replay(self.executable,source,650,optimize=True)
        self.assertEqual(reference,optimized)

    def test_reuse_proof_rejects_state_clocks_rng_and_feedback(self):
        prefix='from lightvm import profile,init,render,rgb,setting\nimport random\nprofile("animation")\n'
        cases=[(' pixels.fill(rgb(1,2,3))\n',True),
               (' pixels.fill(rgb(int(runtime.time)%255,2,3))\n',False),
               (' pixels.fill(rgb(int(runtime.delta)%255,2,3))\n',False),
               (' pixels.fill(rgb(random.randrange(0,255),2,3))\n',False),
               (' ctx.x+=1\n pixels.fill(rgb(ctx.x%255,2,3))\n',False),
               (' pixels[0]=pixels[0]\n',False),
               (' pixels.fill(ctx.color)\n',True)]
        c=Client(ProcessTransport(self.executable))
        try:
            c.configure({'schema':'lightvm.config','outputs':[{'id':'lights','type':'ws2812b','config':{'pin':'GP16','count':17}}]})
            for i,(body,reusable) in enumerate(cases):
                source=prefix+'@init\ndef setup(ctx,runtime):\n ctx.x=0\n ctx.color=setting(rgb(1,2,3))\n@render\ndef draw(ctx,runtime,pixels):\n'+body
                artifact=compile_source(source);self.assertTrue(artifact.success,artifact.diagnostics)
                name='case'+str(i);c.save_program(name,artifact.bytecode)
                c.configure_composition([{'id':'layer','program':name}],output='lights')
                self.assertEqual(c.composition(output='lights')['layers'][0]['render_reusable'],reusable,body)
        finally:c.close()

    def test_deadline_rotation_keeps_many_layers_and_outputs_progressing(self):
        c=Client(ProcessTransport(self.executable))
        try:
            c.configure({'schema':'lightvm.config','outputs':[{'id':f'lights{i}','type':'ws2812b','config':{'pin':f'GP{16+i}','count':200}} for i in range(2)]})
            source='from lightvm import profile,init,render,rgb\nprofile("animation")\n@init\ndef setup(ctx,runtime):\n ctx.x=0\n@render\ndef draw(ctx,runtime,pixels):\n for i in range(1500):\n  ctx.x=(ctx.x+1)%255\n pixels.fill(rgb(ctx.x,0,0))\n'
            artifact=compile_source(source);self.assertTrue(artifact.success,artifact.diagnostics)
            c.save_program('busy',artifact.bytecode)
            for i in range(2):c.configure_composition([{'id':f'layer{j}','program':'busy'} for j in range(8)],output=f'lights{i}')
            def counts():return {f'{i}/{l["id"]}':l['frames'] for i in range(2) for l in c.composition(output=f'lights{i}')['layers']}
            before=counts();time.sleep(0.5);after=counts()
            for key in before:self.assertGreater(after[key],before[key],key)
            self.assertGreater(c.describe()['render_epochs'],0)
        finally:c.close()

    def test_unchanged_background_sleeps_and_inputs_wake_it(self):
        c=Client(ProcessTransport(self.executable))
        try:
            c.configure({'schema':'lightvm.config','outputs':[{'id':'lights','type':'ws2812b','config':{'pin':'GP16','count':200}}]})
            artifact=compile_source((ROOT/'examples/animations/color.py').read_text())
            self.assertTrue(artifact.success,artifact.diagnostics)
            c.save_program('background',artifact.bytecode)
            c.configure_composition([{'id':'bg','program':'background'}],output='lights')
            time.sleep(0.03)
            first=c.composition(output='lights')['layers'][0]
            self.assertTrue(first['render_reusable'])
            time.sleep(0.03)
            self.assertEqual(c.composition(output='lights')['layers'][0]['frames'],first['frames'])
            c.set_inputs({'brightness':{'value':0.4}},output='lights',layer='bg')
            time.sleep(0.03)
            changed=c.composition(output='lights')['layers'][0]
            self.assertGreater(changed['frames'],first['frames'])
            self.assertEqual(changed['displayed_frame'],changed['frames'])
            c.set_inputs({'brightness':{'driver':{'kind':'ramp','from':0.0,'to':1.0,'duration':0.15}}},output='lights',layer='bg')
            time.sleep(0.04)
            ramp=c.composition(output='lights')['layers'][0]
            self.assertGreater(ramp['frames'],changed['frames'])
            c.require_ok(c.command(FRAME,struct.pack('<I',16),output='lights'))
            self.assertGreater(c.composition(output='lights')['layers'][0]['frames'],ramp['frames'])
        finally:c.close()

    def test_cached_background_returns_after_stop_and_resume(self):
        c=Client(ProcessTransport(self.executable))
        try:
            c.configure({'schema':'lightvm.config','outputs':[{'id':'lights','type':'ws2812b','config':{'pin':'GP16','count':3}}]})
            source='from lightvm import profile,render,rgb\nprofile("animation")\n@render\ndef draw(ctx,runtime,pixels):\n pixels.fill(rgb(20,40,60))\n'
            c.save_program('background',compile_source(source).bytecode)
            c.activate('background',output='lights')
            c.command(STOP,output='lights')
            c.resume_program(output='lights')
            time.sleep(.03)
            self.assertEqual(c.snapshot(output='lights')[1],bytes([20,40,60])*3)
            c.configure_composition([{'id':'bg','program':'background'}],output='lights')
            c.require_ok(c.command(STOP,output='lights',layer='bg'))
            self.assertEqual(c.snapshot(output='lights',layer='bg')[1],bytes(12))
            self.assertFalse(c.composition(output='lights')['layers'][0]['dependency_current'])
            c.require_ok(c.command(PLAY,output='lights',layer='bg'))
            time.sleep(.03)
            self.assertEqual(c.snapshot(output='lights')[1],bytes([20,40,60])*3)
            self.assertTrue(c.composition(output='lights')['layers'][0]['dependency_current'])
        finally:c.close()


if __name__ == '__main__': unittest.main()
