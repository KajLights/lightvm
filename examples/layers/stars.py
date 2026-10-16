"""User-defined three-pixel stars over any independent background layer.

The animation chooses births, positions, colours and lifetimes. Sprite callbacks
own expiry and fading; native kernels draw and transform the shared artwork.
"""
import random
from lightvm import (
    profile, setting, rgb, init, render, Sprite, SpriteBatch, PixelArray,
    Transform, ColorTransform, pico_colorwheel,
)

profile("layer/1")

STAR_IMAGE = PixelArray([
     64,  64,  64, 255,
    255, 255, 255, 255,
     64,  64,  64, 255,
], format="rgba8", readonly=True)


def star_init(s, runtime, position, duration, color):
    s.position = position
    s.duration = duration
    s.tint = color


def star_render(s, runtime, pixels):
    if runtime.time >= s.duration:
        s.remove()
        return
    pixels.draw(STAR_IMAGE)
    s.transform = Transform(translate=s.position, origin=1.0)
    s.color = ColorTransform(alpha=1.0 - runtime.time / s.duration, tint=s.tint)


Star = Sprite(width=3, init=star_init, render=star_render)


@init
def setup(ctx, runtime):
    ctx.capacity = setting(max(1, runtime.pixel_count // 10), minimum=1, maximum=runtime.pixel_count)
    ctx.duration = setting(2.0, minimum=0.001)
    ctx.probability = setting(2.0, minimum=0.0)
    ctx.color = setting(rgb(255, 255, 255))
    ctx.random_colors = setting(True)
    ctx.stars = SpriteBatch(Star, capacity=ctx.capacity)
    random.seed()


@render
def draw(ctx, runtime, pixels):
    if random.random() < ctx.probability * runtime.delta:
        color = ctx.color
        if ctx.random_colors:
            color = pico_colorwheel(random.randint(0, 359))
        ctx.stars.spawn(position=random.randrange(runtime.pixel_count), duration=ctx.duration, color=color)
    ctx.stars.render(runtime, pixels)
