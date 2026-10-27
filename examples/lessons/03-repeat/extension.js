// Paint a changing band of colour on any strip length without copying instructions.
export const profile = "animation";
export function render(state, runtime, pixels) {
  for (let position = 0; position < pixels.count; position++) {
    if ((wrap(position, 2) === 0)) {
      pixels[position] = rgb(0, 4, 40);
    } else {
      pixels[position] = rgb(int((60 * (position / pixels.count))), 0, 0);
    }
  }
}
