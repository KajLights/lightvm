// Use one colour rule in several places without copying it.
export const profile = "animation";
function band_colour(position, split) {
  let colour;
  if ((position < split)) {
    colour = rgb(40, 8, 0);
  } else {
    colour = rgb(0, 4, 40);
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
  draw_marker(state, runtime, pixels, (pixels.count - 1), rgb(30, 30, 30));
}
