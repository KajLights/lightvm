// Move a progress bar without editing and restarting the program.
export const profile = "animation";
const input_progress = input("progress", { default: 0.5 });
export function init(state, runtime) {
  state.warning = setting(0.8, { min: 0, max: 1 });
}
export function render(state, runtime, pixels) {
  if ((runtime.input(input_progress) >= state.warning)) {
    pixels.fill(rgb(40, 0, 0));
  } else {
    for (let position = 0; position < pixels.count; position++) {
      if ((position < (runtime.input(input_progress) * pixels.count))) {
        pixels[position] = rgb(40, 8, 0);
      } else {
        pixels[position] = rgb(0, 4, 40);
      }
    }
  }
}
