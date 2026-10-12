"""Paint from both ends and retain earlier paint. Original MIT implementation."""
from lightvm import parameter, rgb, on_start, on_frame
color = parameter("color", rgb(0, 80, 255), kind="rgb8")
speed = parameter("speed", 10.0, minimum=0.1, maximum=1000.0)
progress = 0.0
painted = 0

@on_start
def start(ctx):
    strip = ctx.pixels
    strip.erase(0)

@on_frame(retention="retained")
def draw(ctx):
    global progress, painted
    strip = ctx.pixels
    progress = progress + ctx.delta * speed.value
    until = min((strip.count + 1) // 2, int(progress) + 1)
    for i in range(painted, until):
        strip[i] = color.value
        strip[strip.count - 1 - i] = color.value
    painted = until
