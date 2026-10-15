"""Ordinary bytecode loop writes directly to a checked native pixel view."""
from lightvm_poc import parameter, rgb, kernel
fit = parameter(False)
palette = (rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255))
@kernel
def draw():
    for i in range(count):
        slot = i * len(palette) // count if fit else i % len(palette)
        pixels[i] = palette[slot]
