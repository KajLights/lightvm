// A function can turn a little maths into a reusable idea.
const breathe = seconds => smoothstep(pingpong(seconds / 2));

animation({
  init(state, runtime) {
    state.hue = setting(0.08, { min: 0, max: 1 });
  },
  render(state, runtime, pixels) {
    pixels.fill(hsv(state.hue, 1, breathe(runtime.time)));
  }
});
