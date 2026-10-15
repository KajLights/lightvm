import time
from array import array
from ..helpers import PicoAnimation
from ...easing import SineEaseOut
from ...colors import to_color, to_hex
import random


class Twinkle(PicoAnimation):

    def __init__(self, pixel_object, color="random", brightness = 0.05, twinkle_color = None, duration=2, probability=2, max_stars=None):
        super().__init__(pixel_object)

        self.color = to_color(color) # Ensure color is a tuple
        self.color = (
            int(self.color[0] * brightness),
            int(self.color[1] * brightness),
            int(self.color[2] * brightness),
        )

        if max_stars is None:
            max_stars = len(pixel_object) // 10
        
        if max_stars < 1:
            max_stars = 1
        max_stars = int(max_stars)

        self.twinkle_color = twinkle_color
        self.propability = probability
        self.duration = duration
        self.transition = SineEaseOut(start=1, end=0, duration=duration)
        
        # Our arrays to track the stars - we track the time_monotonic() of the stars birth - or 0 if the star is dead
        self.start_birth_time = array("f", [0] * max_stars)
        
        # The position of the star stored as 16 bit unsigned int - we fill with the max value since it's unlikely we'll have that many pixels
        self.star_positions = array("H", [65535] * max_stars) 

        # The color of the star stored as 3 * 8 bit unsigned int
        self.star_colors = array("B", [0] * 3 * max_stars) 

        # We track deltas since last draw
        self.__last = time.monotonic()


    def draw(self):
        # We're just interested in the delta since the last draw
        elapsed = time.monotonic() - self.__last
        self.__last = time.monotonic()

        spawn = random.random() < (self.propability * elapsed)
        if spawn:
            self.add_star()

        self.pixel_object.fill(self.color)
        self.draw_stars()


    def add_star(self):
        # Find a free slot in the arrays
        for i in range(len(self.start_birth_time)):
            if self.start_birth_time[i] == 0:
                # Star is born now!
                self.start_birth_time[i] = time.monotonic()

                # Find a random unused position for the star
                position = random.randint(0, len(self.pixel_object)-1)
                while self.star_exists_in_position(position):
                    position = random.randint(0, len(self.pixel_object)-1)
                self.star_positions[i] = position

                # Pick a random color for the star
                color = to_color(self.twinkle_color or 'random')
                color_index = i * 3
                self.star_colors[color_index] = color[0]
                self.star_colors[color_index + 1] = color[1]
                self.star_colors[color_index + 2] = color[2]

                return
            
    def star_exists_in_position(self, position):
        for i in range(len(self.star_positions)):
            if self.star_positions[i] == position:
                return True
        return False

    def draw_stars(self):
        for i in range(len(self.start_birth_time)):
            # Only check live stars
            if self.start_birth_time[i] == 0:
                continue

            # Our percentage values are stored in a full byte - so we have 255 = 100%
            star_lifetime = self.__last - self.start_birth_time[i]
            if star_lifetime > self.duration:
                # Star is dead!
                self.start_birth_time[i] = 0
                continue

            # Calculate the brightness of the star
            brightness = self.transition(star_lifetime)
            color_index = i * 3
            r = self.star_colors[color_index] * brightness
            g = self.star_colors[color_index + 1] * brightness
            b = self.star_colors[color_index + 2] * brightness

            # We only fade to the background color
            r = max(self.color[0], r)
            g = max(self.color[1], g)
            b = max(self.color[2], b)

            # Set the pixel to the star's color
            position = self.star_positions[i]
            self.pixel_object[position] = (
                r,
                g,
                b,
            )

    def __str__(self) -> str:
        print(self.color)
        try:
            str = f"twinkle color={to_hex(self.color)} twinkle_color={to_hex(self.twinkle_color)} duration={self.duration} probability={self.propability}"
        except:
            str = "twinkle"
        return str
        
