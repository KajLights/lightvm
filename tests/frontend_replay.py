"""Execute frontend fixtures in the real host VM (called by Vitest)."""
import json
import os
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from lightvm.compiler import compile_source
from client import Client, ProcessTransport, FRAME, STOP


def compile(source):
    result = compile_source(source)
    if not result.success:
        raise ValueError(str(result.diagnostics))
    return result.bytecode


def replay(request):
    with tempfile.TemporaryDirectory() as directory:
        with_client = Client(ProcessTransport(os.environ['LIGHTVM_RECEIVER'], Path(directory) / 'flash.bin'))
        try:
            c = with_client
            count = request.get('count', 5)
            c.configure({'schema': 'lightvm.config', 'outputs': [
                {'id': 'lights', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': count}}]})
            bytecode = compile(request['source'])
            frames = []
            if request.get('transition'):
                prefix = 'from lightvm import profile, render, rgb\nprofile("animation")\n@render\ndef draw(s,r,p):\n p.fill(rgb'
                for name, color in [('a', '(200,100,20)'), ('b', '(20,40,220)')]:
                    c.save_program(name, compile(prefix + color + ')\n'))
                for slot, name in [('A', 'a'), ('B', 'b')]:
                    c.configure_composition([{'id': 'base', 'program': name}], output='lights', slot=slot)
                c.require_ok(c.command(STOP, output='lights'))
                c.save_program('test', bytecode)
                c.select_transition('test', settings=request.get('settings', {}), output='lights')
                for progress in request.get('progress', [0, 50, 100, 25]):
                    c.set_progress(progress, output='lights')
                    c.require_ok(c.command(FRAME, bytes(4), output='lights'))
                    frames.append(list(c.pixel_snapshot))
            else:
                c.upload(bytecode)
                for milliseconds in request.get('frames', [0]):
                    c.require_ok(c.command(FRAME, struct.pack('<I', milliseconds)))
                    frames.append(list(c.pixel_snapshot))
            return frames
        finally:
            with_client.close()


if __name__ == '__main__':
    print(json.dumps(replay(json.load(sys.stdin))))
