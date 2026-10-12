"""A hue gradient advancing in program time. Original MIT implementation."""
from lightvm import parameter, hsv, on_frame
speed = parameter("speed", 0.1, minimum=-10.0, maximum=10.0)
scale = parameter("scale", 1.0, minimum=0.0, maximum=16.0)
phase = 0.0

@on_frame
def draw(ctx):
    global phase
    strip = ctx.pixels
    phase = (phase + ctx.delta * speed.value) % 1.0
    for i in range(strip.count):
        strip[i] = hsv(phase + i * scale.value / strip.count, 1.0, 1.0)
