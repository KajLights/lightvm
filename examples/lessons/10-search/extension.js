// Find a free light, including when no lights are free.
export const profile = "animation";
export function init(state, runtime) {
  state.used = new Int32Array(runtime.pixelCount, { fill: int(1) });
}
export function render(state, runtime, pixels) {
  let slot;
  slot = int(0);
  if ((slot === pixels.count)) {
    pixels.fill(rgb(20, 20, 20));
  }
}
