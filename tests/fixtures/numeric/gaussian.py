"""Gaussian transform of prepared input pairs; no RNG in the kernel."""
import math
from array import array
from lightvm import profile, init, render, rgb
profile('animation')

@init
def setup(ctx, runtime):
    ctx.u1 = array('f', [1 / 65536, 0.125, 0.5, 1 - 1 / 65536])
    ctx.u2 = array('f', [0.0, 0.125, 0.5, 0.875])

@render
def draw(ctx, runtime, pixels):
    offset = int(runtime.time * 10.0)
    for i in range(pixels.count):
        radius = math.sqrt(-2.0 * math.log(ctx.u1[(i + offset) % 4]))
        angle = 6.283185307179586 * ctx.u2[(i // 4) % 4]
        x = radius * math.cos(angle)
        y = radius * math.sin(angle)
        pixels[i] = rgb(int(128.0 + 20.0 * x), int(128.0 + 20.0 * y), 40)
