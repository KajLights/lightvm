"""Six music meters, from the wired end of the strip:

Green level, red bass, amber mid, blue treble, purple centroid position,
and magenta onset envelope with a white flash for a new onset count.
Bind the seven inputs using examples/compositions/audio-features.json.
All drawing runs as ordinary compiled LightVM bytecode.
"""
from lightvm import profile, init, render, Input, hsv

profile("animation")

LEVEL = Input("level", default=0.0, minimum=0.0, maximum=1.0)
BASS = Input("bass", default=0.0, minimum=0.0, maximum=1.0)
MID = Input("mid", default=0.0, minimum=0.0, maximum=1.0)
TREBLE = Input("treble", default=0.0, minimum=0.0, maximum=1.0)
CENTROID = Input("centroid", default=0.0, minimum=0.0, maximum=1.0)
ONSET = Input("onset", default=0.0, minimum=0.0, maximum=1.0)
ONSETS = Input("onsets", default=0, minimum=0, maximum=2147483647)


@init
def setup(ctx, runtime):
    ctx.previous_onsets = -1
    ctx.flash_time = -1.0


@render
def draw(ctx, runtime, pixels):
    level = runtime.input(LEVEL)
    bass = runtime.input(BASS)
    mid = runtime.input(MID)
    treble = runtime.input(TREBLE)
    centroid = runtime.input(CENTROID)
    onset = runtime.input(ONSET)
    onsets = runtime.input(ONSETS)
    if ctx.previous_onsets >= 0 and onsets > 0 and onsets != ctx.previous_onsets:
        ctx.flash_time = runtime.time
    ctx.previous_onsets = onsets
    flash = 0.0
    if ctx.flash_time >= 0.0:
        flash = max(0.0, 1.0 - (runtime.time - ctx.flash_time) / 0.18)

    for i in range(pixels.count):
        section = i * 6 // pixels.count
        position = (i * 6 % pixels.count) / pixels.count
        hue = 0.33
        value = level
        saturation = 1.0
        if section == 1:
            hue = 0.0
            value = bass
        elif section == 2:
            hue = 0.1
            value = mid
        elif section == 3:
            hue = 0.6
            value = treble
        elif section == 4:
            hue = 0.78
            value = 0.0
            if level > 0.02 and abs(position - centroid * 0.9) < 0.05:
                value = 1.0
        elif section == 5:
            hue = 0.88
            value = onset
            saturation = 1.0 - flash
        brightness = 0.015
        if section == 4:
            brightness = max(brightness, value)
        elif position < value * 0.94:
            brightness = 0.7
        if section == 5:
            brightness = max(brightness, flash)
        if position > 0.94:
            brightness = 0.0
        pixels[i] = hsv(hue, saturation, brightness)
