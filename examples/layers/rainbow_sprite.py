"""A 15-pixel rainbow: 10s hue cycle, 20s eased edge-to-edge traversal.

On a 200-pixel strip its translation runs from 0 to 185 and back in 40s.
All colour generation and motion are expressed in the animation language.
"""
from lightvm import (
    profile, setting, init, render, hsv, Sprite, SpriteBatch,
    Transform, ColorTransform,
)
from lightvm.motion import repeat, pingpong, lerp, fit_start, fit_end
from lightvm.easing import smoothstep

profile("layer/1")


def rainbow_init(s, runtime, first, last, color_duration, travel_duration):
    s.first = first
    s.last = last
    s.color_duration = color_duration
    s.travel_duration = travel_duration


def rainbow_render(s, runtime, pixels):
    for i in range(pixels.count):
        pixels[i] = hsv(i / pixels.count, 1.0, 1.0)

    progress = smoothstep(pingpong(runtime.time / s.travel_duration))
    s.transform = Transform(translate=lerp(s.first, s.last, progress))
    s.color = ColorTransform(hue=360.0 * repeat(runtime.time / s.color_duration))


Rainbow = Sprite(width=15, init=rainbow_init, render=rainbow_render)


@init
def setup(ctx, runtime):
    ctx.color_duration = setting(10.0, minimum=0.001)
    ctx.travel_duration = setting(20.0, minimum=0.001)
    ctx.rainbows = SpriteBatch(Rainbow, capacity=1)
    ctx.rainbows.spawn(
        first=fit_start(Rainbow.width, runtime.pixel_count),
        last=fit_end(Rainbow.width, runtime.pixel_count),
        color_duration=ctx.color_duration,
        travel_duration=ctx.travel_duration,
    )


@render
def draw(ctx, runtime, pixels):
    ctx.rainbows.render(runtime, pixels)
