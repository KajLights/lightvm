// This version has a mistake. Find it and explain your repair.
export const profile = "animation";
export function render(state, runtime, pixels) {
  pixels.fill(rgb(0, 0, 0));
  pixels[0] = rgb(40, 8, 0);
  pixels[pixels.count] = rgb(0, 4, 40);
}
