"""The original moving hue gradient; simulation and rendering are separate."""
from lightvm_poc import parameter, hsv, step, prepare, shade
speed = parameter(0.1)
scale = parameter(1.0)
phase = 0.0
stride = 0.0
@step
def update():
    global phase
    phase = (phase + delta * speed) % 1.0
@prepare
def frame():
    global stride
    stride = scale / count
@shade
def pixel():
    return hsv(phase + index * stride, 1.0, 1.0)
