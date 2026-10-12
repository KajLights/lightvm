"""Seeded procedural star lifetimes; no embedded background or star arrays.

Density controls the fraction of each pixel's period spent visible. A polynomial
phase offset supplies variation; fading is derived from the cubic equation.
Original MIT implementation; this is not picolights' star-slot simulation.
"""
from lightvm import pixels, parameter, rgb, hash01, on_frame
from lightvm.easing import cubic_out
strip = pixels("A")
seed = parameter("seed", 1, minimum=0, maximum=65520)
density = parameter("density", 0.15, minimum=0.01, maximum=1.0)
lifetime = parameter("lifetime", 1.5, minimum=0.01, maximum=60.0)

@on_frame
def draw(ctx):
    for i in range(strip.count):
        phase = (ctx.time * density.value / lifetime.value + hash01(i, seed.value)) % 1.0
        if phase < density.value:
            brightness = 1.0 - cubic_out(phase / density.value)
            strip[i] = rgb(int(255.0 * brightness), int(220.0 * brightness), int(160.0 * brightness))
