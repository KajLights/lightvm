"""Choose a colour during initialization, then fill every render.

"""
import random

from lightvm import profile, setting, rgb, init, render, pico_colorwheel

profile("animation/1")


@init
def setup(ctx, runtime):
    color = setting(rgb(255, 64, 0))
    random_color = setting(True)

    random.seed()
    ctx.color = color
    if random_color:
        ctx.color = pico_colorwheel(random.randint(0, 359))


@render
def draw(ctx, runtime, pixels):
    pixels.fill(ctx.color)
