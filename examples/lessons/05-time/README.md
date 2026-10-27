# Make time visible

Make a light blink once per second while the controls stay usable.

The frame function is called again and again. Elapsed time tells us which picture to draw now.

1. **Predict:** What should be visible at 0, 0.999, 1 and 2 seconds?
2. **Try:** Open a starter in Python, JavaScript or Blocks. Start with 25 lights.
3. **Change:** Use elapsed whole seconds to choose between orange and dark.
4. **Check:** Try time steps of 0, 0.033, 0.25 and 1 second. A time step of 0 should not change the picture.
5. **Build on it:** Replace the sudden change with a slow rise and fall in brightness.
6. **Explain:** Explain why you do not need a forever loop or a sleep instruction here.

Each language has four files: `starter`, `solution`, `extension` and `bug`.
Try the task before opening the solution. The bug is deliberate. The broken version repeats forever inside one frame. No complete frame reaches the lights.

[Back to the course](../README.md)
