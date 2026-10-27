// Make small lights appear, fade and leave room for the next ones.
export const profile = "animation";
export function init(state, runtime) {
  state.trail = new PixelArray(runtime.pixelCount);
  random.seed(int(int(7)));
  state.next_light = 0;
}
export function render(state, runtime, pixels) {
  state.trail.multiplyAlpha(Math.exp((-2 * runtime.delta)));
  if ((runtime.time >= state.next_light)) {
    state.trail.point(wrapIndex(random.integer(int(int(0)), int(int((pixels.count - 1)))), state.trail.length), rgb(40, 8, 0));
    state.next_light = (runtime.time + 0.2);
  }
  pixels.draw(state.trail, { transform: new Transform({ translate: 0, scale: 1 }), overflow: "clip" });
}
