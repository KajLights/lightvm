// A retained PixelArray remembers the trail between frames.
export function init(state, runtime) {
  state.image = new PixelArray(runtime.pixelCount);
  state.speed = setting(0.2, { min: 0, max: 2 });
}
export function render(state, runtime, pixels) {
  state.image.multiplyAlpha(Math.max(0, 1 - runtime.delta * 3));
  const position = pingpong(runtime.time * state.speed) * (pixels.count - 1);
  state.image.point(position, rgb(255, 80, 10));
  pixels.draw(state.image);
}
