# Give lights a life

Make small lights appear, fade and leave room for the next ones.

Choose a random place once per new light. A seed lets you replay those choices. A saved image remembers the trail.

1. **Predict:** Will asking for a random place inside every pixel loop make one firefly or many unrelated choices?
2. **Try:** Open a starter in Python, JavaScript or Blocks. Start with 25 lights.
3. **Change:** Choose a new place every 0.2 seconds and fade the old lights by elapsed time.
4. **Check:** Restart twice: the first random choices should match. Try a delayed frame. A long delay creates at most one new light.
5. **Build on it:** Replace the random place with a moving place that wraps around the ring.
6. **Explain:** Explain the difference between one random choice per new light and one per drawn pixel. Stored colour steps round, and fast movement can skip pixels.

Each language has four files: `starter`, `solution`, `extension` and `bug`.
Try the task before opening the solution. The bug is deliberate. The broken version uses a negative fade rate. The trail stays bright instead of fading.

[Back to the course](../README.md)
