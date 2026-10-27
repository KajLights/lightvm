// Change several lights together without hunting for repeated colour numbers.
export const profile = "animation";
export function render(state, runtime, pixels) {
  let colour, middle_colour;
  colour = rgb(40, 8, 0);
  pixels[0] = colour;
  pixels[(pixels.count - 1)] = colour;
  middle_colour = rgb(0, 4, 40);
  pixels[int((pixels.count / 2))] = middle_colour;
}
