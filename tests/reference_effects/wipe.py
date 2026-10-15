"""Redraw growing paint from both ends using scalar progress."""
from lightvm import parameter, rgb, on_frame
color = parameter("color", rgb(0, 80, 255), kind="rgb8")
speed = parameter("speed", 10.0, minimum=0.1, maximum=1000.0)
progress = 0.0

@on_frame
def draw(ctx):
    global progress
    strip = ctx.pixels
    progress = progress + ctx.delta * speed.value
    until = min((strip.count + 1) // 2, int(progress) + 1)
    for i in range(until):
        strip[i] = color.value
        strip[strip.count - 1 - i] = color.value
