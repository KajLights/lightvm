// This version has a mistake. Find it and explain your repair.
export const profile = "animation";
export function render(state, runtime, pixels) {
  let position;
  for (position of range(int((pixels.count + 1)))) {
    pixels[position] = rgb(int((60 * (position / pixels.count))), 0, 0);
  }
}
