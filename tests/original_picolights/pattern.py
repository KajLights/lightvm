from ..helpers import PicoAnimation
from ...easing import *
from ...colors import to_color, random_color, to_hex
import random

def decode_pattern(pattern):
    for p in pattern.split(","):
        length, color = p.split("#")
        if length == '':
            length = 1
        color = to_color("#" + color)
        for _ in range(int(length)):
            yield color

def random_pattern():
    for _ in range(random.randint(2, 8)):
        yield random_color()


class Pattern(PicoAnimation):
   
    def __init__(self, pixels, pattern=None, to_fit=True):
        super().__init__(pixels)
        if pattern:
            self.__orig_pattern = pattern
            self.pattern = list(decode_pattern(pattern))
        else:
            self.pattern = list(random_pattern())
            self.__orig_pattern = ",".join([to_hex(c) for c in self.pattern])

        if isinstance(to_fit, str):
            to_fit = to_fit.lower() in ["true", "1", "yes", "y", "t"]
        self.to_fit = to_fit


    def draw(self):
        pattern_length = len(self.pattern)
        if self.to_fit:
            scale = len(self.pixel_object) / pattern_length
        else:
            scale = 1

        for p_ix in range(len(self.pixel_object)):
            pattern_ix = int(p_ix / scale)
            if pattern_ix >= pattern_length:
                pattern_ix = pattern_ix % pattern_length
            self.pixel_object[p_ix] = self.pattern[pattern_ix] if pattern_ix < len(self.pattern) else self.pattern[-1]


    def __str__(self) -> str:
        str = f"pattern pattern={self.__orig_pattern} to_fit={self.to_fit}"
        return str
