// This version has a mistake. Find it and explain your repair.
export const profile = "animation";
function band_colour(position, split) {
  let colour;
  if ((position < split)) {
    colour = rgb(40, 8, 0);
  }
  return colour;
}
function draw_marker(state, runtime, pixels, position, colour) {
  pixels[position] = colour;
}
export function render(state, runtime, pixels) {
  for (let position = 0; position < pixels.count; position++) {
    pixels[position] = band_colour(position, (pixels.count / 2));
  }
  draw_marker(state, runtime, pixels, 0, rgb(30, 30, 30));
}
