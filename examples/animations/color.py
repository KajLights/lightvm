"""A solid colour or composition background, following the light colour by default.

A preset can bind color to a fixed colour or any RGB source instead.
"""
from lightvm import profile, render, Input, rgb

profile("animation")
COLOR = Input("color", default=rgb(255, 64, 0), control="color")


@render
def draw(ctx, runtime, pixels):
    pixels.fill(runtime.input(COLOR))
