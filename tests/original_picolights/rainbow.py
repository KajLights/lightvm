from picolights.colors import rgb_to_hsv, hsv_to_rgb, to_color, to_hex
from picolights.colors._colors import colorwheel
from rainbowio import colorwheel as rainbowio_colorwheel
from ..helpers import TimedAnimation
from micropython import const

def linear_interpolate_color(color1, color2, t):
    return tuple(int((1 - t) * c1 + t * c2) for c1, c2 in zip(color1, color2))

MAX_HUE = const(255)

class Rainbow(TimedAnimation):

    def __init__(
            self, 
            pixel_object,
            color="random", 
            color_count=30, 
            color_n=1.5,
            duration=60, 
            scale=1, 
        ):
        super(Rainbow, self).__init__(pixel_object, duration=duration)
        self._scale = scale
        self.length = len(self.pixel_object)
        self.reversed = color_count < 0
        self.color_count = int(abs(color_count))
        self.scale = scale * MAX_HUE / self.length

        self.color = color = to_color(color)
        self.hsv = rgb_to_hsv(*color)
        self.hue = self.hsv[0] * MAX_HUE / 360

    def draw(self):
        start_hue = self.hue + (self.progress * MAX_HUE)
        for i in range(self.length):
            hue = start_hue + (i * self.scale)
            self.pixel_object[i] = rainbowio_colorwheel(hue)

    def __str__(self) -> str:
        str = f"rainbow duration={self.duration} color={to_hex(self.color)} color_count={self.color_count} scale={self._scale}"
        return str

