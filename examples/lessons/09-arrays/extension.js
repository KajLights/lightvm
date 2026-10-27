// Give each light its own brightness and change one without changing the rest.
export const profile = "animation";
export function init(state, runtime) {
  state.brightness = new Float32Array(runtime.pixelCount, { fill: 0.08 });
  state.brightness[int(0)] = 0.25;
}
export function render(state, runtime, pixels) {
  for (let position = 0; position < pixels.count; position++) {
    state.brightness[position] = (state.brightness[position] * Math.exp((-1 * runtime.delta)));
  }
  for (let position = 0; position < pixels.count; position++) {
    pixels[position] = hsv(0.08, 1, state.brightness[position]);
  }
}
