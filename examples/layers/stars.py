"""Transparent stars; backgrounds are independent scene layers.

SpriteBatch owns bounded state and a free-position pool. Aging, expiry, drawing,
colour scaling and alpha blending run in bounded native batches.
"""
import random
from lightvm import profile, setting, rgb, init, render, SpriteBatch, pico_colorwheel

profile("layer/1")


@init
def setup(ctx, runtime):
    ctx.capacity = setting(max(1, runtime.pixel_count // 10), minimum=1, maximum=runtime.pixel_count)
    ctx.duration = setting(2.0, minimum=0.001)
    ctx.probability = setting(2.0, minimum=0.0)
    ctx.color = setting(rgb(255, 255, 255))
    ctx.random_colors = setting(True)
    ctx.stars = SpriteBatch(ctx.capacity)
    random.seed()


@render
def draw(ctx, runtime, pixels):
    ctx.stars.advance(runtime.delta)
    if random.random() < ctx.probability * runtime.delta:
        color = ctx.color
        if ctx.random_colors:
            color = pico_colorwheel(random.randint(0, 359))
        ctx.stars.spawn(color, ctx.duration)
    pixels.sprites(ctx.stars)
