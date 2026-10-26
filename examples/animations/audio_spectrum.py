"""Sixteen fixed rainbow sections, with brightness driven by spectral energy.

Use examples/compositions/audio-spectrum.json: four instances each bind four
adjacent bands, keeping ordinary scalar INPUTs and the existing renderer.
Low frequencies start red at the wired end; high frequencies end violet.
One shared loudness reference levels all bands together, preserving their
relative strengths. Quiet gaps hold the reference rather than raising gain.
"""
from lightvm import profile, init, render, setting, Input, hsv

profile("animation")

BAND0 = Input("band0", default=0.0, minimum=0.0, maximum=1.0)
BAND1 = Input("band1", default=0.0, minimum=0.0, maximum=1.0)
BAND2 = Input("band2", default=0.0, minimum=0.0, maximum=1.0)
BAND3 = Input("band3", default=0.0, minimum=0.0, maximum=1.0)
ONSETS = Input("onsets", default=0, minimum=0, maximum=2147483647)
LEVEL = Input("level", default=0.0, minimum=0.0, maximum=1.0)


@init
def setup(ctx, runtime):
    ctx.first_band = setting(0, minimum=0, maximum=12)
    ctx.headroom = setting(0.32, minimum=0.0, maximum=0.8)
    ctx.span = setting(0.40, minimum=0.01, maximum=1.0)
    ctx.reference = 0.28
    ctx.level_ready = 0
    ctx.previous_onsets = -1
    ctx.flash_time = -1.0


@render
def draw(ctx, runtime, pixels):
    band0 = runtime.input(BAND0)
    band1 = runtime.input(BAND1)
    band2 = runtime.input(BAND2)
    band3 = runtime.input(BAND3)
    onsets = runtime.input(ONSETS)
    level = runtime.input(LEVEL)
    if level > 0.15:
        target = max(0.28, level)
        if ctx.level_ready == 0:
            ctx.reference = target
            ctx.level_ready = 1
        # Fast reduction of gain after volume rises, slow recovery when it falls.
        tau = 3.0
        if target > ctx.reference:
            tau = 0.18
        ctx.reference += (target - ctx.reference) * runtime.delta / (tau + runtime.delta)
    gate = min(1.0, max(0.0, (level - 0.10) / 0.10))
    if ctx.previous_onsets >= 0 and onsets > 0 and onsets != ctx.previous_onsets:
        ctx.flash_time = runtime.time
    ctx.previous_onsets = onsets
    flash = 0.0
    if ctx.flash_time >= 0.0:
        flash = max(0.0, 1.0 - (runtime.time - ctx.flash_time) / 0.18)

    for local_band in range(4):
        band = ctx.first_band + local_band
        value = band0
        if local_band == 1:
            value = band1
        elif local_band == 2:
            value = band2
        elif local_band == 3:
            value = band3
        brightness = gate * min(1.0, max(0.0, (value - ctx.reference + ctx.headroom) / ctx.span))
        color = hsv(band * 0.78 / 15.0, 1.0 - flash, max(brightness, flash))
        # Fill each section once, including the alternating 12/13-pixel widths.
        pixels.fill(color, start=(band * pixels.count + 15) // 16,
                    stop=((band + 1) * pixels.count + 15) // 16)
