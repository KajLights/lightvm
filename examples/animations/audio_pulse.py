"""A whole-strip music pulse that stays readable when the strip is coiled.

Level and bass control amber brightness; new onset occurrences briefly turn
the whole strip white. floor/span expand the useful normalized input range.
Bind inputs with examples/compositions/audio-pulse.json.
"""
from lightvm import profile, init, render, setting, Input, hsv

profile("animation")

LEVEL = Input("level", default=0.0, minimum=0.0, maximum=1.0)
BASS = Input("bass", default=0.0, minimum=0.0, maximum=1.0)
ONSETS = Input("onsets", default=0, minimum=0, maximum=2147483647)


@init
def setup(ctx, runtime):
    ctx.floor = setting(0.35, minimum=0.0, maximum=0.95)
    ctx.span = setting(0.35, minimum=0.01, maximum=1.0)
    ctx.previous_onsets = -1
    ctx.flash_time = -1.0


@render
def draw(ctx, runtime, pixels):
    energy = 0.4 * runtime.input(LEVEL) + 0.6 * runtime.input(BASS)
    energy = min(1.0, max(0.0, (energy - ctx.floor) / ctx.span))
    onsets = runtime.input(ONSETS)
    if ctx.previous_onsets >= 0 and onsets > 0 and onsets != ctx.previous_onsets:
        ctx.flash_time = runtime.time
    ctx.previous_onsets = onsets
    flash = 0.0
    if ctx.flash_time >= 0.0:
        flash = max(0.0, 1.0 - (runtime.time - ctx.flash_time) / 0.18)
    pixels.fill(hsv(0.07, 1.0 - flash, max(0.85 * energy * energy, flash)))
