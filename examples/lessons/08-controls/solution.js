// Move a progress bar without editing and restarting the program.
export const profile = "animation";
const input_progress = input("progress", { default: 0.5 });
export function render(state, runtime, pixels) {
  for (let position = 0; position < pixels.count; position++) {
    if ((position < (runtime.input(input_progress) * pixels.count))) {
      pixels[position] = rgb(40, 8, 0);
    } else {
      pixels[position] = rgb(0, 4, 40);
    }
  }
}
