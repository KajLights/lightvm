// Move a progress bar without editing and restarting the program.
export const profile = "animation";
export function render(state, runtime, pixels) {
  for (let position = 0; position < pixels.count; position++) {
    if ((position < (0.5 * pixels.count))) {
      pixels[position] = rgb(40, 8, 0);
    } else {
      pixels[position] = rgb(0, 4, 40);
    }
  }
}
