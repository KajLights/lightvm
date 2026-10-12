"""Repeat or stretch an immutable palette. Original MIT implementation."""
from lightvm import parameter, rgb, on_frame
fit = parameter("fit", False)
palette = (rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255))

@on_frame
def draw(ctx):
    strip = ctx.pixels
    for i in range(strip.count):
        index = i * len(palette) // strip.count if fit.value else i % len(palette)
        strip[i] = palette[index]
