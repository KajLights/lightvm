// This version has a mistake. Find it and explain your repair.
export const profile = "animation";
export function render(state, runtime, pixels) {
  for (let position = 0; position < pixels.count; position++) {
    pixels[position] = hsv(0.08, 1, state.brightness[position]);
  }
}
