// Make the first light orange and the last light blue.
export const profile = "animation";
export function render(state, runtime, pixels) {
  pixels.fill(rgb(0, 0, 0));
  pixels[int((pixels.count / 2))] = rgb(30, 30, 30);
}
