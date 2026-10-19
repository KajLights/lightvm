// A transition can be scrubbed in either direction using the Mixer.
export const profile = "transition";
export function init(state, runtime) {
  state.reverse = setting(false);
}
export function render(state, runtime, pixels) {
  wipe(pixels, smoothstep(runtime.progress), { reverse: state.reverse });
}
