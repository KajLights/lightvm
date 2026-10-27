// Change several lights together without hunting for repeated colour numbers.
export const profile = "animation";
export function render(state, runtime, pixels) {
  pixels[0] = rgb(40, 8, 0);
  pixels[(pixels.count - 1)] = rgb(40, 8, 0);
}
