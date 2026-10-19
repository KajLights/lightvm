// Try changing the ramp duration or the scale input.
export const profile = "animation";
const phase = input("phase", { default: 0, min: 0, max: 1,
  driver: ramp({ duration: 20, mode: "repeat" }) });
const scale = input("scale", { default: 1, min: 0, max: 16 });

export function init(state, runtime) {
  state.brightness = setting(1, { min: 0, max: 1 });
}

export function render(state, runtime, pixels) {
  for (let i = 0; i < pixels.count; i++) {
    const hue = repeat(runtime.input(phase) + i / pixels.count * runtime.input(scale));
    pixels[i] = hsv(hue, 1, state.brightness);
  }
}
