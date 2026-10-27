// Make a light blink once per second while the controls stay usable.
export const profile = "animation";
export function render(state, runtime, pixels) {
  pixels.fill(rgb(40, 8, 0));
}
