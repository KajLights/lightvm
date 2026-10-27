# Remember movement

Move a dot at the same speed even when some frames arrive late.

Remembered values survive the next frame. Seconds since the last frame tells us how far to move.

1. **Predict:** At 8 pixels per second, how far does the dot move in a quarter of a second?
2. **Try:** Open a starter in Python, JavaScript or Blocks. Start with 25 lights.
3. **Change:** Add elapsed seconds times speed to the remembered position, then wrap at the end.
4. **Check:** Run at 0.016 and 0.25 seconds per frame. Compare distance after the same total time.
5. **Build on it:** Use a speed of minus 8 to travel backwards around the ring.
6. **Explain:** Explain the difference between a local position and state.position.

Each language has four files: `starter`, `solution`, `extension` and `bug`.
Try the task before opening the solution. The bug is deliberate. The broken version moves one pixel per frame. It changes speed when the frame time changes.

[Back to the course](../README.md)
