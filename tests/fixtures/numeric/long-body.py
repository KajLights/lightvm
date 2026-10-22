"""Maximum 128-instruction pure body; each update retains a varying operand."""
from lightvm import profile, render, rgb
profile("animation")

@render
def draw(ctx, runtime, pixels):
    for i in range(pixels.count):
        x = i
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        x = x + 1
        pixels[i] = rgb(x % 255, 0, 0)
