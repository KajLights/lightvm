from lightvm_poc import parameter, rgb, step, shade
color = parameter(rgb(255, 0, 64))
speed = parameter(12.0)
position = 0.0
@step
def update():
    global position
    position = (position + delta * speed) % count
@shade
def pixel():
    if index == int(position):
        return color
    return rgb(0, 0, 0)
