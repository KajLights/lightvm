"""Live spatial scale, smooth motion and a trigger counter selecting the field."""
from lightvm import profile, Input, Ramp, render, simplex3, hsv
profile("animation")

SCALE = Input("scale", default=4.0, minimum=0.1, maximum=32.0)
PHASE = Input("phase", default=0.0, minimum=0.0, maximum=1.0,
              driver=Ramp(duration=30.0, mode="pingpong"))
# A producer can increment this latched count on a trigger. Repeated samples
# retain the same field; skipped counts select the newest field directly.
FIELD = Input("field", default=0)
LEVEL = Input("level", default=1.0, minimum=0.0, maximum=1.0)

@render
def draw(ctx, runtime, pixels):
    scale = runtime.input(SCALE)
    phase = runtime.input(PHASE)
    field = runtime.input(FIELD)
    level = runtime.input(LEVEL)
    for i in range(pixels.count):
        n = simplex3(i / pixels.count * scale, phase * 4.0, 0.25, field)
        brightness = (n + 1.0) * 0.5 * level
        pixels[i] = hsv(0.6 + n * 0.15, 1.0, brightness)
