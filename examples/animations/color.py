"""Fill the strip with one colour, directly or as a scene background.

Modes (initialization settings):
  random_color=True: choose a random hue once at initialization (default).
  random_color=False: use color, for example color=rgb(255, 64, 0).
The chosen colour stays fixed until restart; ordinary writes are opaque.
"""
import random

from lightvm import (
    profile, setting, init, render, rgb, pico_colorwheel,
)

profile("animation")


@init
def setup(ctx, runtime):
    ctx.color = setting(rgb(255, 64, 0))
    ctx.random_color = setting(True)

    random.seed()
    ctx.chosen_color = ctx.color
    if ctx.random_color:
        ctx.chosen_color = pico_colorwheel(random.randint(0, 359))


@render
def draw(ctx, runtime, pixels):
    pixels.fill(ctx.chosen_color)
