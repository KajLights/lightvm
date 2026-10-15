from ..helpers import PicoAnimation
from ...easing import easing as easing_lookup
from ...easing import *
from ...colors import to_color, to_hex

transitions = [CubicEaseIn, CubicEaseOut, CubicEaseInOut, LinearInOut, SineEaseIn, SineEaseOut, SineEaseInOut]

class Color(PicoAnimation):

    def __init__(self, pixel_object, color="random"):
        super().__init__(pixel_object)
        self.color = to_color(color)

    def draw(self):
        self.pixel_object.fill(self.color)

    def __str__(self) -> str:
        return f"color color={to_hex(self.color)}"