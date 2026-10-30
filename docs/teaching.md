# Learn with lights

These programming lessons are part of KajLights' broader aim: learning
electronics, computing and networking through microcontrollers. The
[project goals](project-goals.md) explain that direction. The
[website activities](https://kajlights.com/learn/) connect the lessons to wiring
a circuit, running a program on a board and following messages between devices.
The twelve lessons below focus on programming; they are not the whole curriculum.

Open **Editor → Learn with lights**, choose a language and a lesson, and press
**Start preview**. Start with 25 lights. Switch on **Show a ring** to put the
same positions around a circle; position 0 is at the top. No controller is needed.

The [twelve lessons](../examples/lessons/README.md) each include a starting
program, one solution, a next change and a deliberate mistake in Python,
JavaScript and Blocks. Stay with one language while learning an idea. The other
versions are there when you want to compare ways of expressing it.

## A first session

1. Predict which lights the starting program will paint.
2. Run it. Point out position 0 and the last position, `count - 1`.
3. Change one colour or position. Predict again before running.
4. Try five lights and trace each instruction together.
5. Save locally, open a different example, then import your saved file.

Allow 30–45 minutes for each lesson, with extra discussion time where useful.
Start with instructions and names, then loops, choices, time, remembered values,
functions, controls, arrays, searches, fireflies and a lighthouse. Every lesson
asks the learner to explain a result and make a change. Copying a solution alone
does not show understanding.

The examples keep colours fairly dim. The preview is a drawing of the lights;
it does not predict electrical brightness or power use. On physical strips,
teacher setup includes the configured count, the first light and the controller's
brightness setting.

## Try one frame or one instruction

A frame is one finished picture. A loop paints several positions in that picture;
it does not pause to show a new picture after every instruction.

**Next frame** advances by the selected number of seconds. Try 0, 0.033, 0.25 and
1 second. **Play** keeps drawing; **Pause** finishes the current frame before
pausing. **Stop and clear** immediately discards the local run. Changing code,
pixel count, starting settings or stepping mode also starts a fresh run.

Enable **Step through the code** before starting to pause at statement boundaries.
**Show line** or **Show block** points into the editor. **Watch a value** displays
up to 32 scalar names at the current pause, including remembered values. Arrays
are drawn through the lights; this first inspector does not expand their contents.
Only a finished frame replaces the picture. A bad index or an endless loop stops
with a message and a source location. Earlier changes to remembered values are
not undone by a fault; restart before trying the repair.

The local preview uses the checked C VM compiled to WebAssembly. It accepts
1–512 lights and uses a bounded memory pool and the same frame work limit as the
controller. Stepping disables bulk loop optimisation so that source statements
can be followed. Use normal playback for performance comparisons. Browser timing
does not predict controller FPS. Transitions use **Mixer** and **Preview on device**
because they need two prepared scenes.

## Values that change

Local names start fresh on each call. Values on `state` survive into the next
frame. Create remembered values and arrays in the starting function. A starting
setting is read when the run starts; changing it requires restart.

Live inputs can change between frames. The controls below the local preview
accept values after a frame finishes. They take effect on the next frame.
Manual values and starting settings are included in **Save locally**, and restored
by **Import file**. Timer and device-source bindings keep their saved recipe,
but the local preview uses the declared starting value until you choose a manual
value. Test automatic drivers and physical sources on the controller.

Raw `.py`, `.js` and `.blocks.json` files contain source alone. Use `.lightvm.json`
when you want to keep settings and input choices too. Shape, pixel count and
time-step controls belong to the current preview session.

## Supported everyday syntax

These are bounded teaching languages, not general Python or browser JavaScript.
Numbers, colours, Boolean values and arrays keep their types. Arrays have fixed
lengths. There are no arbitrary objects, dynamic imports, exceptions, recursive
functions or allocations during drawing. Advanced numeric and drawing blocks
remain available under **Show all blocks**.

| Idea | Python | JavaScript | Blocks |
| --- | --- | --- | --- |
| Name a value | `colour = rgb(40, 8, 0)` | `const colour = rgb(40, 8, 0);` | Set a local name |
| Remember a value | `state.position = 0.0` in init | `state.position = 0;` in init | Set a remembered name in Start |
| Every position | `for position in range(pixels.count):` | `for (let position = 0; position < pixels.count; position++)` | For each pixel |
| Count backwards | `range(count - 1, -1, -1)` | ordinary descending `for`, or `for (position of range(...))` with whole-number arguments | Count from / up to / step |
| Read an array | `for value in state.levels:` | `for (const value of state.levels)` | For each value in array |
| Make a choice | `if ...: ... else: ...` | `if (...) { ... } else { ... }` | If / else |
| Return a result | `def shade(position, split): ... return colour` | `function shade(position, split) { ... return colour; }` | Calculation with up to three input names |
| Draw using a function | `def marker(pixels, position): ...` | `function marker(pixels, position) { ... }` | Drawing function, then call drawing function |
| Wrap a position | `wrap_index(position, count)` | `wrapIndex(position, count)` | Wrap position on a ring |
| Wrap a moving value | `wrap(position, count)` | `wrap(position, count)` | Wrap / modulo with a positive length |
| Replay choices | `random.seed(7)` | `random.seed(7)` | Start the same random choices |

Python loop targets keep their last assigned value. Reassigning a loop target
does not change the next value from `range`. An empty loop preserves an existing
name and does not create a new value. Give a starting value when a loop might
not run. Python supports one-, two- and three-argument ranges, chained comparisons,
simple tuple swaps, power, constant defaults and named helper arguments.

Python `and`/`or` and JavaScript `&&`/`||` return an operand, evaluating only what
is needed. Result operands need matching scalar types. Blocks checks Boolean
sockets for logic. A name assigned on both sides of a choice can be used afterward;
otherwise give it a starting value. In JavaScript declare it outside the braces
if it is needed after the choice.

Python `%` and JavaScript `%` differ for negative numbers. The `wrap` helpers
make reverse motion clear in either language. Python integers use 32 bits;
JavaScript ordinary numbers and Python fractional numbers use 32-bit floats.
Use `int` at a whole-number boundary when necessary. Native arrays offer
`fill(value)` and `copy_from(other)` / `copyFrom(other)` for matching types and
lengths. For full details see [Python](animation-profile.md),
[JavaScript and Blocks](javascript-and-blocks.md), and [motion](motion.md).

## Building an editor that works without internet

Use Node 22.13 or newer, Python 3.12 or newer, and Emscripten 4.0.7 (the tested
version). Install the pinned npm packages with `npm ci`. The
[official SDK instructions](https://emscripten.org/docs/tools_reference/emsdk.html)
explain how to install and activate Emscripten. The repository's CI installs
4.0.7 through `.github/actions/browser-vm`.

```sh
npm ci
EMCC=/path/to/emsdk/upstream/emscripten/emcc npm run build
python3 -m http.server 8765 --directory build/web --bind 127.0.0.1
```

Open `http://127.0.0.1:8765/`. The full `build/web` directory contains the compiler,
Python runtime, code editor, lessons and VM. Serve that complete directory on a
local computer or classroom server. `file://` is not supported. This is a local
static distribution, not a service-worker installation: a previously visited
online page alone is not an offline guarantee. Firmware installation may still
use external services; lessons and the local preview do not.

`npm run dev` uses the same preview assets: run `npm run build:preview` first.
The production prebuild rebuilds when VM sources change; `EMCC` can select the
compiler. Runtime dependency versions are pinned in `package-lock.json` and
redistribution notices are copied into the output. See
[Pyodide's local deployment guide](https://pyodide.org/en/stable/usage/downloading-and-deploying.html).

## Reproducing the acceptance checks

```sh
cmake -S . -B build/teaching-host -DLIGHTVM_PLATFORM=host -DCMAKE_BUILD_TYPE=Release
cmake --build build/teaching-host --parallel
ctest --test-dir build/teaching-host --output-on-failure
node tools/build_teaching.mjs --check
LIGHTVM_NUMERIC_RUNTIME="$PWD/build/teaching-host/test_platform" npm run test:lessons -- --frames
LIGHTVM_RECEIVER="$PWD/build/teaching-host/lightvm" LIGHTVM_NUMERIC_RUNTIME="$PWD/build/teaching-host/test_platform" npm test
python3 -m unittest discover -s tests -p 'test_teaching_preview.py'
CHROME_EXECUTABLE=/path/to/chrome node tools/check_teaching_browser.cjs
```

The last check expects the local server above. It blocks remote requests and
checks first startup, strip/ring display, stepping, source errors, lesson loading,
saved settings/inputs, Blocks and a narrow screen. Regenerate course source with
`npm run build:lessons` after changing `tools/teaching_lessons.mjs`.

[Implementation and measured limits](design/teaching-implementation.md) ·
[Beginner observation session](teaching-pilot.md)
