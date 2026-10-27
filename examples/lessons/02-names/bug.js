// This version has a mistake. Find it and explain your repair.
export const profile = "animation";
export function render(state, runtime, pixels) {
  let colour;
  colour = rgb(40, 8, 0);
  colour = 0.5;
  pixels[0] = colour;
  pixels[(pixels.count - 1)] = colour;
}
