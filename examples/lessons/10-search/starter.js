// Find a free light, including when no lights are free.
export const profile = "animation";
export function init(state, runtime) {
  state.used = new Int32Array(runtime.pixelCount, { fill: int(0) });
}
export function render(state, runtime, pixels) {
  state.used[int(0)] = int(1);
  for (let position = 0; position < pixels.count; position++) {
    if ((state.used[position] === int(1))) {
      pixels[position] = rgb(40, 8, 0);
    } else {
      pixels[position] = rgb(0, 4, 40);
    }
  }
}
