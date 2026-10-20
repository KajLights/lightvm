"""User-defined three-pixel stars over any independent background layer.

The animation chooses births, positions, colours and lifetimes. Sprite callbacks
own expiry and fading; native kernels draw and transform the shared artwork.

Modes (initialization settings):
  random_colors=True: give each new star a random hue (default).
  random_colors=False: tint every star with color, default rgb(255, 255, 255).
  duration: seconds from birth to complete fade, default 2.0.
  probability: birth attempts per second, default 2.0. Each render tests
    probability * delta and can spawn at most one star. capacity limits the
    number of concurrent stars.
Run directly over black, or layer these transparent stars over any background.
The live brightness input scales existing and new stars without restarting their
lifetimes. Background brightness belongs to the independent background layer.
"""
import random

from lightvm import (
    profile, setting, init, render, Input, rgb, pico_colorwheel,
    Sprite, SpriteBatch, PixelArray, Transform, ColorTransform,
)

profile("animation")

BRIGHTNESS = Input("brightness", default=1.0, minimum=0.0, maximum=1.0)

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
    s.color = ColorTransform(alpha=1.0 - runtime.time / s.duration, tint=s.tint,
                             brightness=runtime.input(BRIGHTNESS))


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
