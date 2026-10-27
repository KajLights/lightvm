// This version has a mistake. Find it and explain your repair.
export const profile = "animation";
export function init(state, runtime) {
  state.position = 0;
  state.speed = 8;
}
export function render(state, runtime, pixels) {
  state.position = wrap((state.position + 1), pixels.count);
  pixels[int(state.position)] = rgb(40, 8, 0);
}
