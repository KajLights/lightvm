"""Time-indexed births: work is driven by elapsed time, not render RNG trials."""
from lightvm import profile, init, render, Sprite, SpriteBatch, PixelArray, Transform, ColorTransform
profile('animation')
IMAGE=PixelArray([64,64,64,255,255,255,255,255,64,64,64,255],format='rgba8',readonly=True)
def birth(s,runtime,position,age):
 s.transform=Transform(translate=position,origin=1.0)
 s.age=age
def paint(s,runtime,pixels):
 age=s.age+runtime.time
 if age>=2.0:
  s.remove()
  return
 pixels.draw(IMAGE)
 s.color=ColorTransform(alpha=1.0-age/2.0)
Star=Sprite(width=3,init=birth,render=paint)
@init
def setup(ctx,runtime):
 # Include expired slots awaiting this frame's callbacks while births catch up.
 ctx.stars=SpriteBatch(Star,capacity=32)
 ctx.next_birth=0.0
 ctx.sequence=0
@render
def draw(ctx,runtime,pixels):
 while runtime.time>=ctx.next_birth:
  age=runtime.time-ctx.next_birth
  if age<2.0:
   ctx.stars.spawn(position=(ctx.sequence*73+7)%runtime.pixel_count,age=age)
  ctx.next_birth+=0.2
  ctx.sequence+=1
 ctx.stars.render(runtime,pixels)
