"""Explicit fixed triangular ramp with lazy, nested colour selection."""
from lightvm import profile, render, rgb, fixed
profile('animation')

@render
def draw(ctx, runtime, pixels):
    phase = fixed(runtime.time % 1.0)
    width = fixed(pixels.count)
    for i in range(pixels.count):
        p = (phase + fixed(i) / width) % fixed(1)
        envelope = fixed(2) * p if p < fixed(0.5) else fixed(2) * (fixed(1) - p)
        green = 0
        if p < fixed(0.25):
            green = 30
        else:
            green = 60 if p < fixed(0.75) else 90
        pixels[i] = rgb(int(fixed(255) * envelope), green, 17)
