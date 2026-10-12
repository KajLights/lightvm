"""Full-coverage colour, with a live RGB8 parameter. Original MIT implementation."""
from lightvm import pixels, parameter, rgb, on_frame
strip = pixels("A")
color = parameter("color", rgb(255, 64, 0), kind="rgb8")

@on_frame
def draw(ctx):
    strip.fill(color.value)
