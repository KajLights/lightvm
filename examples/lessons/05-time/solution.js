// Make a light blink once per second while the controls stay usable.
export const profile = "animation";
export function render(state, runtime, pixels) {
  if ((wrap(int(runtime.time), int(2)) === int(0))) {
    pixels.fill(rgb(40, 8, 0));
  } else {
    pixels.fill(rgb(0, 0, 0));
  }
}
