from adafruit_led_animation.animation import Animation

class Wipe(Animation):

    on_cycle_complete_supported = True

    def __init__(self, *args, step=1, **kwargs):
        super(Wipe, self).__init__(*args, **kwargs)
        self.step = step

    def draw(self):
        cycle = self.draw_count - 1
        if cycle > len(self.pixel_object) / (2 * self.step):
            self.cycle_complete = True
            return
        
        start = cycle * self.step
        self.pixel_object[start:start+self.step] = [self.color] * self.step

        start = len(self.pixel_object) - (cycle * self.step) - self.step
        self.pixel_object[start:start+self.step] = [self.color] * self.step
        
