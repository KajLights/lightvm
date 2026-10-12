"""A moving marker with retained scalar phase. Original MIT implementation."""
from lightvm import pixels, parameter, rgb, on_frame
strip = pixels("A")
color = parameter("color", rgb(255, 0, 64), kind="rgb8")
speed = parameter("speed", 12.0, minimum=-1000.0, maximum=1000.0)
position = 0.0

@on_frame
def draw(ctx):
    global position
    position = (position + ctx.delta * speed.value) % strip.count
    strip[int(position)] = color.value
