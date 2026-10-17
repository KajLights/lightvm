"""Rainbow gradient with a repeating duration-based colour cycle.

Modes (initialization settings):
  scale=1.0: one spatial colour wheel across the strip (default).
  scale=0.0: a uniform colour that cycles over time; larger values repeat the wheel.
  random_color=True: choose the initial hue randomly once (default).
  random_color=False: start from color's hue, for example color=rgb(255, 0, 0).
duration is seconds per colour cycle, default 60.0. This paints the whole strip;
rainbow_sprite.py demonstrates a smaller moving image over transparency.
"""
import random

from lightvm import (
    profile, setting, init, render, rgb, pico_colorwheel, hue_degrees, colorwheel,
)

profile("animation")


@init
def setup(ctx, runtime):
    ctx.duration = setting(60.0, minimum=0.001)
    ctx.scale = setting(1.0, minimum=0.0)

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
    progress = (runtime.time % ctx.duration) / ctx.duration
    start_hue = ctx.hue + progress * 255
    spatial_scale = ctx.scale * 255 / pixels.count
    for i in range(pixels.count):
        pixel_hue = start_hue + i * spatial_scale
        pixels[i] = colorwheel(pixel_hue)
