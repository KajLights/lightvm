"""A supplied RGB pattern, stretched to fit the strip by default.
"""
import random
from array import array

from lightvm import profile, setting, rgb, init, render, pico_colorwheel

profile("animation/1")


@init
def setup(ctx, runtime):
    ctx.to_fit = setting(True)

    random.seed()
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
