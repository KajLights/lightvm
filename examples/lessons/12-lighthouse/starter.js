// Build a lighthouse beam that crosses the ring seam, then explain how it works.
export const profile = "animation";
export function init(state, runtime) {
  state.position = 0;
  state.speed = 8;
}
export function render(state, runtime, pixels) {
  state.position = wrap((state.position + (runtime.delta * state.speed)), pixels.count);
  pixels[int(state.position)] = rgb(40, 8, 0);
}
