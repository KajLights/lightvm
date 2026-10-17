"""Moving eyes, retained wipes and timed sprite trails in one animation.

Motion modes (initialization settings):
  motion=0: loop from one edge to the other, then jump back to the start.
  motion=1: bounce between the edges (default, a Cylon-style eye).
  motion=2: traverse once, then hold at the far edge.
  motion=3: repeat an inward wipe; paired eyes start at opposite edges.
  reverse=True: reverse any path, including inward to outward wipes.
duration is seconds per traversal; a bounce round trip takes twice this.
eased=True uses cubic easing; eased=False gives constant travel speed.

Paint modes:
  retain=False, samples=1: draw only the current eye, clearing between frames.
  retain=False, trail_seconds>0.0, samples>1: draw fading historical eye samples.
  retain=True: keep the swept range painted until the next cycle; motion=2
    keeps it indefinitely. This overrides trail_seconds; use samples=1.

Recipes (set these defaults in setup, or use initialization settings):
  Clean Cylon: motion=1, retain=False, samples=1.
  Chase then hold: motion=2, eye_count=1, retain=True, eased=False.
  Inward wipe: motion=3, eye_count=2, retain=True, eased=False,
    rainbow_eyes=False, samples=1. reverse=True makes it an outward wipe.
  Trail: motion=1, duration=2.0, trail_seconds=0.6, samples=8.

eye_count controls the number of eyes. rainbow_eyes=True gives them distinct
hues; False uses color. phase offsets the path in traversal units (2 per bounce
round trip). Use eye_count=1 for separate scene layers with their own blend mode.

History is sampled from the path at fixed ages, independent of display frames;
no previous-frame images or accumulated 8-bit alpha are needed. Sparse samples
can show separated ghost eyes. Each reserved sample costs 104 bytes and runs a
full sprite callback per frame, even if retain or zero trail_seconds makes the
copies redundant. Keep samples=1 for clean eyes and retained wipes. More samples
increase both CPU work and RAM; two eyes with samples=24 mean 48 sprite renders.
On the 200-LED Pico demo alongside three other outputs, eye_width=24,
duration=2.0, trail_seconds=0.6 and samples=8 improved playback from about 7 fps
with 24 samples to 17 fps. These are workload measurements, not a guaranteed rate.
"""
from lightvm import (
    profile, setting, init, render, rgb, hsv,
    Sprite, SpriteBatch, Transform, ColorTransform,
)
from lightvm.motion import repeat, pingpong, clamp01, lerp
from lightvm.easing import cubic_in_out

profile("animation/1")


def progress(time, duration, motion, eased, phase):
    value = time / duration + phase
    if motion == 1:
        value = pingpong(value)
    elif motion == 2:
        value = clamp01(value)
    else:
        value = repeat(value)
    if eased:
        value = cubic_in_out(value)
    return value


def eye_init(s, runtime, first, last, width, duration, motion, eased, phase,
             tint, retain, trail_seconds, delay):
    s.first = first
    s.last = last
    s.width = width
    s.duration = duration
    s.motion = motion
    s.eased = eased
    s.phase = phase
    s.tint = tint
    s.retain = retain
    s.trail_seconds = trail_seconds
    s.delay = delay
    s.initial = lerp(first, last, progress(0.0, duration, motion, eased, phase))


def eye_render(s, runtime, pixels):
    time = runtime.time - s.delay
    if time < 0.0:
        return
    p = progress(time, s.duration, s.motion, s.eased, s.phase)
    position = lerp(s.first, s.last, p)
    width = float(s.width)
    opacity = 1.0
    if s.retain:
        cycle = 1.0
        if s.motion == 1:
            cycle = 2.0
        q = time / s.duration + s.phase
        cycle_start = q - q % cycle
        start = s.first
        if s.motion == 2 or cycle_start <= s.phase:
            start = s.initial
        low = min(start, position)
        high = max(start, position)
        # Include a reversal crossed since this eye began painting.
        if s.motion == 1 and q % 2.0 >= 1.0:
            if cycle_start > s.phase or s.phase % 2.0 <= 1.0:
                low = min(low, s.last)
                high = max(high, s.last)
        width = high - low + width
        position = low
    elif s.trail_seconds > 0.0:
        opacity = max(0.0, 1.0 - s.delay / s.trail_seconds)
    pixels.fill(s.tint)
    s.transform = Transform(translate=position + (width - 1.0) / 2.0, scale=width)
    s.color = ColorTransform(alpha=opacity)


Eye = Sprite(width=1, init=eye_init, render=eye_render)


@init
def setup(ctx, runtime):
    ctx.eye_count = setting(2, minimum=1, maximum=32)
    ctx.eye_width = setting(max(1, runtime.pixel_count // 10), minimum=1, maximum=runtime.pixel_count)
    ctx.duration = setting(1.0, minimum=0.001)
    ctx.motion = setting(1, minimum=0, maximum=3)
    ctx.eased = setting(True)
    ctx.phase = setting(0.0)
    ctx.reverse = setting(False)
    ctx.color = setting(rgb(255, 0, 0))
    ctx.rainbow_eyes = setting(True)
    ctx.retain = setting(False)
    ctx.trail_seconds = setting(0.0, minimum=0.0)
    ctx.samples = setting(1, minimum=1, maximum=128)
    ctx.eyes = SpriteBatch(Eye, capacity=ctx.eye_count * ctx.samples)
    for i in range(ctx.eye_count):
        first = 0.0
        last = float(runtime.pixel_count - ctx.eye_width)
        phase = ctx.phase
        if ctx.motion == 3:
            last = last / 2.0
            if i % 2 == 1:
                first = float(runtime.pixel_count - ctx.eye_width)
            phase = phase + (i // 2) / max(1, (ctx.eye_count + 1) // 2)
        elif ctx.motion != 2:
            cycle = 1.0
            if ctx.motion == 1:
                cycle = 2.0
            phase = phase + cycle * i / ctx.eye_count
        if ctx.reverse:
            previous_first = first
            first = last
            last = previous_first
        tint = ctx.color
        if ctx.rainbow_eyes:
            tint = hsv(i / ctx.eye_count, 1.0, 1.0)
        # Oldest samples first, so the current opaque eye paints last.
        for j in range(ctx.samples):
            delay = 0.0
            if not ctx.retain:
                delay = ctx.trail_seconds * (ctx.samples - j - 1) / max(1, ctx.samples - 1)
            ctx.eyes.spawn(
                first=first, last=last, width=ctx.eye_width,
                duration=ctx.duration, motion=ctx.motion, eased=ctx.eased,
                phase=phase, tint=tint, retain=ctx.retain,
                trail_seconds=ctx.trail_seconds, delay=delay,
            )


@render
def draw(ctx, runtime, pixels):
    ctx.eyes.render(runtime, pixels, sampling="nearest")
