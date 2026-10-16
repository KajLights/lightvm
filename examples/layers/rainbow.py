"""The colourwheel is drawn across the output by one bounded native kernel."""
from lightvm import profile, setting, init, render

profile("layer/1")


@init
def setup(ctx, runtime):
    ctx.duration = setting(3.3, minimum=0.001)
    ctx.scale = setting(1.0, minimum=0.0)


@render
def draw(ctx, runtime, pixels):
    phase = (runtime.time % ctx.duration) / ctx.duration * 255.0
    pixels.colorwheel(phase, ctx.scale * 255.0 / runtime.pixel_count)
