"""Native bounded fill of the core-owned buffer."""
from lightvm_poc import parameter, rgb, kernel
color = parameter(rgb(255, 64, 0))
@kernel
def draw():
    pixels.fill(color)
