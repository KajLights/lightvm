export function init(state, runtime) {
  state.table = new FixedArray(runtime.pixelCount, { fill: fixed(0.75) + fixed(0.75) });
  state.factor = fixed(1.5) + fixed(1.5);
}

export function render(state, runtime, pixels) {
  for (let i = int(0); i < pixels.count; i++) {
    const q = state.table[i];
    let v = int(0);
    if (i < int(16)) {
      v = int(q * state.factor);
    } else {
      v = int(fixedSqrt(q));
    }
    pixels[i] = rgb(v & int(31), int(Math.sqrt(4)), 7);
  }
}
