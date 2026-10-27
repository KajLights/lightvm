// Split the strip into coloured sections, with a warning colour near the end.
export const profile = "animation";
export function render(state, runtime, pixels) {
  for (let position = 0; position < pixels.count; position++) {
    if ((position === int((pixels.count / 2)))) {
      pixels[position] = rgb(30, 30, 30);
    } else {
      if ((position >= (pixels.count * 0.75))) {
        pixels[position] = rgb(40, 0, 0);
      } else {
        if ((position < (pixels.count / 2))) {
          pixels[position] = rgb(40, 8, 0);
        } else {
          pixels[position] = rgb(0, 4, 40);
        }
      }
    }
  }
}
