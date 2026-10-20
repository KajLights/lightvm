"""A solid colour or composition background, following the light colour by default.

A preset can bind color to a fixed colour or any RGB source instead. Local
brightness scales this layer independently of the output's master dimmer.
"""
from lightvm import profile, render, Input, rgb, ColorTransform

profile("animation")
COLOR = Input("color", default=rgb(255, 64, 0), control="color")
BRIGHTNESS = Input("brightness", default=1.0, minimum=0.0, maximum=1.0)


@render
def draw(ctx, runtime, pixels):
    pixels.fill(runtime.input(COLOR))
    pixels.transform(ColorTransform(brightness=runtime.input(BRIGHTNESS)))
