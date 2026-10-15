"""A moving marker with persistent scalar phase."""
from lightvm import parameter, rgb, on_frame
color = parameter("color", rgb(255, 0, 64), kind="rgb8")
speed = parameter("speed", 12.0, minimum=-1000.0, maximum=1000.0)
position = 0.0

@on_frame
def draw(ctx):
    global position
    strip = ctx.pixels
    position = (position + ctx.delta * speed.value) % strip.count
    strip[int(position)] = color.value
