// This version has a mistake. Find it and explain your repair.
export const profile = "animation";
const input_progress = input("progress", { default: 0.5 });
export function render(state, runtime, pixels) {
  pixels[(int((runtime.input(input_progress) * pixels.count)) + int(1))] = rgb(40, 8, 0);
}
