import time
from rainbowio import colorwheel

from ..helpers import PicoAnimation

class Chase(PicoAnimation):

    on_cycle_complete_supported = True

    def draw(self):
        pixel_len = len(self.pixel_object)
        cur_pix = (self.draw_count - 1) % pixel_len

        self.pixel_object[cur_pix] = self.color

        if self.draw_count % len(self.pixel_object) == 0:
            self.cycle_complete = True

