"""A supplied RGB pattern, stretched to fit the strip by default.

Modes (initialization settings):
  to_fit=True: stretch the complete pattern across the strip (default).
  to_fit=False: repeat its original pixels without stretching.
Edit ctx.pattern's RGB entries below to supply different artwork.
"""
from array import array

from lightvm import (
    profile, setting, init, render, rgb,
)

profile("animation")


@init
def setup(ctx, runtime):
    ctx.to_fit = setting(True)

    ctx.pattern = array("I", [rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255)])


@render
def draw(ctx, runtime, pixels):
    pattern_length = len(ctx.pattern)
    scale = 1.0
    if ctx.to_fit:
        scale = pixels.count / pattern_length
    for i in range(pixels.count):
        pattern_index = int(i / scale) % pattern_length
        pixels[i] = ctx.pattern[pattern_index]
