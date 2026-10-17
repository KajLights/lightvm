"""Reveal B across A, with an optional three-pixel white separator sprite.

reverse=True reveals from the opposite end. separator=True paints a white
33% / 100% / 33% peak at the wipe boundary; endpoints remain the exact scenes.
"""
from lightvm import profile, init, render, setting, PixelArray, Sprite, SpriteBatch, Transform
from lightvm.transitions import wipe

profile("transition")

SEPARATOR_IMAGE = PixelArray([
     85,  85,  85, 255,
    255, 255, 255, 255,
     85,  85,  85, 255,
], format="rgba8", readonly=True)


def separator_init(s, runtime, span, reverse):
    s.span = span
    s.reverse = reverse


def separator_render(s, runtime, pixels):
    boundary = int(runtime.progress * s.span)
    if s.reverse:
        boundary = s.span - boundary
    pixels.draw(SEPARATOR_IMAGE, sampling="nearest")
    s.transform = Transform(translate=boundary, origin=1.0)


Separator = Sprite(width=3, init=separator_init, render=separator_render)


@init
def setup(ctx, runtime):
    ctx.reverse = setting(False)
    ctx.separator = setting(False)
    ctx.separators = SpriteBatch(Separator, capacity=1)
    ctx.separators.spawn(span=runtime.pixel_count, reverse=ctx.reverse)


@render
def draw(ctx, runtime, pixels):
    wipe(pixels, runtime.progress, reverse=ctx.reverse)
    if ctx.separator:
        ctx.separators.render(runtime, pixels, sampling="nearest")
