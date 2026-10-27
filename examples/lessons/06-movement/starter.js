// Move a dot at the same speed even when some frames arrive late.
export const profile = "animation";
export function init(state, runtime) {
  state.position = 0;
  state.speed = 8;
}
export function render(state, runtime, pixels) {
  pixels[int(state.position)] = rgb(40, 8, 0);
}
