"""Prepared repeating colour table, motivated by Pattern and Colors."""
from array import array
from lightvm import profile, init, render, rgb
profile('animation')

@init
def setup(ctx, runtime):
    ctx.table = array('i', [0x102030, 0xA04020, 0x2040C0])

@render
def draw(ctx, runtime, pixels):
    offset = int(runtime.time * 10.0) - 19
    for i in range(pixels.count):
        color = ctx.table[(i + offset) % len(ctx.table)]
        pixels[i] = rgb((color >> 16) & 255, (color >> 8) & 255, color & 255)
