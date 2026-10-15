"""Rainbow gradient with a repeating duration-based colour cycle.

"""
import random

from lightvm import profile, setting, rgb, init, render, pico_colorwheel, hue_degrees, colorwheel

profile("animation/1")


@init
def setup(ctx, runtime):
    ctx.duration = setting(60.0, minimum=0.001)
    ctx.scale = setting(1.0, minimum=0.0)

    color = setting(rgb(255, 0, 0))
    random_color = setting(True)

    random.seed()
    chosen_color = color
    if random_color:
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
