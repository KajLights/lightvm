from lightvm import parameter, rgb, step, prepare, shade
color = parameter(rgb(0, 80, 255))
speed = parameter(10.0, minimum=0.1, maximum=1000.0)
progress = 0.0
until = 0
@step
def update():
    global progress
    progress = progress + delta * speed
@prepare
def frame():
    global until
    until = min((count + 1) // 2, int(progress) + 1)
@shade
def pixel():
    if index < until:
        return color
    if count - 1 - index < until:
        return color
    return rgb(0, 0, 0)
