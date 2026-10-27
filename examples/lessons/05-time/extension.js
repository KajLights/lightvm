// Make a light blink once per second while the controls stay usable.
export const profile = "animation";
export function render(state, runtime, pixels) {
  pixels.fill(hsv(0.08, 1, (0.2 * pingpong((runtime.time / 2)))));
}
