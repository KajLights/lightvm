"""An opaque solid layer, reusable as a background for any foreground."""
from lightvm import profile, setting, rgb, init, render

profile("layer/1")


@init
def setup(ctx, runtime):
    ctx.color = setting(rgb(255, 64, 0))


@render
def draw(ctx, runtime, pixels):
    pixels.fill(ctx.color)
