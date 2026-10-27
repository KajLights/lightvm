// This version has a mistake. Find it and explain your repair.
export const profile = "animation";
function draw_beam(state, runtime, pixels, front) {
  let behind, position, colour;
  for (behind of range(int(3))) {
    position = (int(front) - behind);
    if ((behind === int(0))) {
      colour = rgb(40, 20, 0);
    } else {
      colour = rgb(10, 2, 0);
    }
    pixels[position] = colour;
  }
}
export function init(state, runtime) {
  state.position = 0;
  state.speed = 8;
}
export function render(state, runtime, pixels) {
  state.position = wrap((state.position + (runtime.delta * state.speed)), pixels.count);
  draw_beam(state, runtime, pixels, state.position);
}
