"""Fill from both ends towards the centre, then clear and repeat.

Speed is steps per second; each step adds step_width pixels at each end.
The initial frame includes the first step. Partial final steps are clipped.
"""
from lightvm import profile, setting, rgb, init, render

profile("animation/1")


@init
def setup(ctx, runtime):
    ctx.color = setting(rgb(255, 64, 0))
    ctx.step_width = setting(1, minimum=1, maximum=runtime.pixel_count)
    ctx.speed = setting(50.0, minimum=0.0)
    ctx.progress = 0.0


@render
def draw(ctx, runtime, pixels):
    steps_per_cycle = (pixels.count + 2 * ctx.step_width - 1) // (2 * ctx.step_width)
    ctx.progress = (ctx.progress + runtime.delta * ctx.speed) % steps_per_cycle
    lit_count = min(pixels.count, (int(ctx.progress) + 1) * ctx.step_width)
    pixels.fill(rgb(0, 0, 0))
    pixels.fill(ctx.color, start=0, stop=lit_count)
    pixels.fill(ctx.color, start=pixels.count - lit_count, stop=pixels.count)
