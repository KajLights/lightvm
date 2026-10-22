from lightvm import profile,init,render,rgb,hsv,PixelArray,Transform
profile('animation')
from lightvm import Sprite,SpriteBatch,ColorTransform
IMAGE=PixelArray([64,64,64,255,255,255,255,255,64,64,64,255],format="rgba8",readonly=True)
def setup_star(s,runtime,position):
 s.position=position
def draw_star(s,runtime,pixels):
 pixels.draw(IMAGE)
 s.transform=Transform(translate=s.position,origin=1.0)
 s.color=ColorTransform(alpha=0.75)
Star=Sprite(width=3,init=setup_star,render=draw_star)
@init
def setup(ctx,runtime):
 ctx.stars=SpriteBatch(Star,capacity=12)
 for i in range(12):
  ctx.stars.spawn(position=(i*17)%runtime.pixel_count)
@render
def draw(ctx,runtime,pixels):
 ctx.stars.render(runtime,pixels)
