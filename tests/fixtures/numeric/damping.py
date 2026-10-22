"""Ordered fixed damping followed by independent branch shading."""
from lightvm import profile, init, render, rgb, fixed, fixed_from_bits
profile('animation')

@init
def setup(ctx, runtime):
    ctx.x = fixed_from_bits(52429)
    ctx.v = fixed(0)

@render
def draw(ctx, runtime, pixels):
    impulse = fixed_from_bits(1311) if int(runtime.time * 10.0) % 2 == 0 else fixed_from_bits(-1311)
    ctx.v = (ctx.v + impulse) * fixed_from_bits(58982)
    ctx.x = min(fixed(1), max(fixed_from_bits(36045), ctx.x + ctx.v * fixed_from_bits(22938)))
    for i in range(pixels.count):
        shade = ctx.x if i % 2 == 0 else fixed(1) - ctx.x
        pixels[i] = rgb(int(shade * fixed(255)), 10, 30)
