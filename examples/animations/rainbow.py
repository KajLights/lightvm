"""Rainbow gradient with wireable phase and spatial scale.

Modes (initialization settings):
  random_color=True: choose the initial hue randomly once (default).
  random_color=False: start from color's hue, for example color=rgb(255, 0, 0).
Live inputs: phase is a colour cycle in turns; its default driver repeats every
60 seconds. scale=1.0 fits one spatial wheel, 0.0 is uniform, and larger values
repeat the wheel. Override the phase driver to change its duration or source.
This paints the whole strip;
rainbow_sprite.py demonstrates a smaller moving image over transparency.
"""
import random

from lightvm import (
    profile, setting, init, render, Input, Ramp, rgb, pico_colorwheel, hue_degrees, colorwheel,
)

profile("animation")

PHASE = Input("phase", default=0.0, minimum=0.0, maximum=1.0, unit="turns",
              driver=Ramp(duration=60.0, mode="repeat"))
SCALE = Input("scale", default=1.0, minimum=0.0, maximum=16.0)


@init
def setup(ctx, runtime):
    ctx.color = setting(rgb(255, 0, 0))
    ctx.random_color = setting(True)

    random.seed()
    chosen_color = ctx.color
    if ctx.random_color:
        hue_index = random.randint(0, 359)
        chosen_color = pico_colorwheel(hue_index)

    ctx.hue = hue_degrees(chosen_color) * 255 / 360


@render
def draw(ctx, runtime, pixels):
    progress = runtime.input(PHASE) % 1.0
    start_hue = ctx.hue + progress * 255
    spatial_scale = runtime.input(SCALE) * 255 / pixels.count
    for i in range(pixels.count):
        pixel_hue = start_hue + i * spatial_scale
        pixels[i] = colorwheel(pixel_hue)
