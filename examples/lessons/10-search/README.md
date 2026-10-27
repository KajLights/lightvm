# Stop when done

Find a free light, including when no lights are free.

A while loop repeats until its condition becomes false. Check the array length before reading an entry.

1. **Predict:** Where should the search stop when every entry is already used?
2. **Try:** Open a starter in Python, JavaScript or Blocks. Start with 25 lights.
3. **Change:** Walk forward while the slot is used, then paint it only if it is inside the array.
4. **Check:** Run enough frames to fill the strip. The search must finish both before and after the final slot is filled.
5. **Build on it:** Start with every slot filled. Show a white strip when the search finds no free slot.
6. **Explain:** Explain why checking slot < count comes before reading used[slot].

Each language has four files: `starter`, `solution`, `extension` and `bug`.
Try the task before opening the solution. The bug is deliberate. The broken search never advances its slot. The first frame works; the next frame gets stuck.

[Back to the course](../README.md)
