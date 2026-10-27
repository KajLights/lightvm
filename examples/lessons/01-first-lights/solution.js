// Make the first light orange and the last light blue.
export const profile = "animation";
export function render(state, runtime, pixels) {
  pixels.fill(rgb(0, 0, 0));
  pixels[0] = rgb(40, 8, 0);
  pixels[(pixels.count - 1)] = rgb(0, 4, 40);
}
