"""A language-defined rainbow; independent pixel calculations compile in bulk."""
from lightvm import profile, setting, init, render, colorwheel

profile("layer/1")


@init
def setup(ctx, runtime):
    ctx.duration = setting(3.3, minimum=0.001)
    ctx.scale = setting(1.0, minimum=0.0)


@render
def draw(ctx, runtime, pixels):
    phase = (runtime.time % ctx.duration) / ctx.duration * 255.0
    stride = ctx.scale * 255.0 / runtime.pixel_count
    for i in range(pixels.count):
        pixels[i] = colorwheel(phase + i * stride)
