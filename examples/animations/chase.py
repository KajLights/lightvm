"""Accumulate colour from the first pixel to the last, then remain filled.

Speed is pixels per second; one pixel is lit in the initial frame.
"""
from lightvm import profile, setting, rgb, init, render

profile("animation/1")


@init
def setup(ctx, runtime):
    ctx.color = setting(rgb(255, 64, 0))
    ctx.speed = setting(50.0, minimum=0.0)
    ctx.progress = 1.0


@render
def draw(ctx, runtime, pixels):
    ctx.progress = min(float(pixels.count), ctx.progress + runtime.delta * ctx.speed)
    lit_count = int(ctx.progress)
    pixels.fill(rgb(0, 0, 0))
    pixels.fill(ctx.color, start=0, stop=lit_count)
