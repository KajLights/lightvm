"""A costly body stays ordered when its first pixel exceeds the kernel work cap."""
import math
from lightvm import profile, render, rgb
profile("animation")

@render
def draw(ctx, runtime, pixels):
    scale = 2048.0 + runtime.time % 1.0
    for i in range(pixels.count):
        x = (i + 1) / scale
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        x = math.exp(math.log(x))
        pixels[i] = rgb(int(x * 200.0), 0, 0)
