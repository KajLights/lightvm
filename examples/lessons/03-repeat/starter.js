// Paint a changing band of colour on any strip length without copying instructions.
export const profile = "animation";
export function render(state, runtime, pixels) {
  for (let position = 0; position < pixels.count; position++) {
    pixels[position] = rgb(40, 8, 0);
  }
}
