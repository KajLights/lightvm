// Find a free light, including when no lights are free.
export const profile = "animation";
export function init(state, runtime) {
  state.used = new Int32Array(runtime.pixelCount, { fill: int(0) });
}
export function render(state, runtime, pixels) {
  let slot;
  slot = int(0);
  while (((slot < pixels.count) && (state.used[slot] !== int(0)))) {
    slot = (slot + int(1));
  }
  if ((slot < pixels.count)) {
    state.used[slot] = int(1);
  }
  for (let position = 0; position < pixels.count; position++) {
    if ((state.used[position] === int(1))) {
      pixels[position] = rgb(40, 8, 0);
    } else {
      pixels[position] = rgb(0, 4, 40);
    }
  }
}
