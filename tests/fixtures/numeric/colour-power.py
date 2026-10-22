"""Trigonometric colour components, powers and lazy channel selection."""
import math
from array import array
from lightvm import profile, init, render, rgb
profile('animation')

@init
def setup(ctx, runtime):
    ctx.exponents = array('f', [0.5, 1.0, 2.0, 4.0])

@render
def draw(ctx, runtime, pixels):
    phase = runtime.time % 1.0
    for i in range(pixels.count):
        angle = (i / pixels.count + phase) * 6.283185307179586
        s = math.sin(angle)
        value = math.pow(abs(s), ctx.exponents[i % 4])
        red = 0
        blue = 0
        if s < 0.0:
            red = int(value * 255.0)
            blue = 0
        else:
            red = 0
            blue = int(value * 255.0)
        pixels[i] = rgb(red, 20, blue)
