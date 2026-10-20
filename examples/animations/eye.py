"""A wireable eye: live position, width, colour and intensity.

Position is normalized across the range that fits the current eye width.
Width is a fraction of the strip; zero hides the eye. Colour follows the light
by default; local intensity is 0..1 and can follow music. The default position driver bounces, taking two seconds
per traversal. Override any input without restarting the existing sprite.
Sweep remains the separate example for retained paint and historical trails.
"""
from lightvm import (
    profile, init, render, Input, Ramp, rgb,
    PixelArray, Sprite, SpriteBatch, Transform, ColorTransform,
)

profile("animation")

POSITION = Input("position", default=0.0, minimum=0.0, maximum=1.0,
                 driver=Ramp(duration=2.0, mode="pingpong", easing="smoothstep"))
WIDTH = Input("width", default=0.1, minimum=0.0, maximum=1.0, unit="fraction")
COLOR = Input("color", default=rgb(255, 0, 0), control="color")
INTENSITY = Input("intensity", default=1.0, minimum=0.0, maximum=1.0)

IMAGE = PixelArray([
     85,  85,  85, 255,
    255, 255, 255, 255,
     85,  85,  85, 255,
], readonly=True)


def eye_init(s, runtime, strip_count):
    s.strip_count = strip_count


def eye_render(s, runtime, pixels):
    width = runtime.input(WIDTH) * s.strip_count
    if width <= 0.0:
        return
    width = max(1.0, width)
    left = runtime.input(POSITION) * (s.strip_count - width)
    pixels.draw(IMAGE)
    s.transform = Transform(translate=left + (width - 1.0) / 2.0,
                            scale=width / 3.0, origin=1.0)
    s.color = ColorTransform(tint=runtime.input(COLOR),
                             brightness=runtime.input(INTENSITY))


Eye = Sprite(width=3, init=eye_init, render=eye_render)


@init
def setup(ctx, runtime):
    ctx.eyes = SpriteBatch(Eye, capacity=1)
    ctx.eyes.spawn(strip_count=float(runtime.pixel_count))


@render
def draw(ctx, runtime, pixels):
    ctx.eyes.render(runtime, pixels)
