# KajLights: purpose and learning goals

KajLights helps people understand electronics, computing and networking by
making things with microcontrollers. Lights and sound give them something they
can change, observe and investigate. Understanding how the parts work together
helps them imagine and build projects of their own.

This is the shared direction for the website and LightVM's educational work.
Use it when choosing examples, planning learning material or reviewing public
copy. The [website implementation plan](design/website-implementation-plan.md)
describes the technical delivery; it does not replace these goals.

## What we want people to discover

The learning path should connect three subjects through practical projects:

| Subject | A useful experience | What it helps someone understand |
| --- | --- | --- |
| Electronics | Connect power, ground and data; change an LED's colour without changing the wiring. | Components have different jobs. Power supplies energy; a signal carries information. Voltage, current and connections affect how a circuit works. |
| Computing | Make a dot move, inspect its remembered position, then run it on the board with the browser closed. | A microcontroller is a computer with a processor and memory. Programs use instructions, state and time to produce behaviour. |
| Networking and communications | Save a nickname over USB and read it over Wi-Fi; later switch a saved pattern through MQTT. | Computers exchange messages using addresses and protocols. A program can run independently of the connection used to control it. Lost or late messages need a considered response. |

Programming is part of this experience. Recognising components, tracing a
circuit, understanding where a program runs, testing an explanation and deciding
what a connected object should do are equally relevant. The aim is to help
people reason about a whole system and use its parts in new combinations.

## Understanding, imagination and AI

“If you can't imagine it, you can't ask for it to be built.”

AI can help implement an idea. Experience with electronics and computing gives
someone more ideas to bring to it, the language to describe them and a way to
judge the result. Knowing that a board can keep time, control individual lights
and receive messages suggests a countdown, a game or a sign controlled from
another room.

Use concrete requests to demonstrate this connection. For example, adapt the
time lesson into ten blue lights that go out one per second, then flash green
three times. A learner can describe the intended behaviour, predict the display
at particular times and compare it with the result. AI is an optional tool for
making and explaining; no AI account is needed to participate.

This shares the aim of [Found in Space](https://foundin.space/about/): give
people something to explore, access to how it works and materials to create
with. Found in Space uses scientific data and models; KajLights uses circuits,
microcontrollers and messages. Play, investigation and making are all worthwhile
ways to take part, without a compulsory sequence.

## How to teach through the project

- Make the first build achievable for a child of about ten with an adult helper.
  Prepare the board, connect the lights and change a colour. Use that success to
  open a question or a next idea.
- Introduce an idea when it explains something the learner can observe or do.
  A moving dot gives memory and time a purpose; a remote command gives an
  address and a protocol a purpose.
- Give an activity an action, an observable result and something to investigate.
  Let people change examples and make their own designs. Avoid requiring a
  lecture before the first useful experiment.
- Keep the first Pico build simple: USB power through VBUS, common ground and
  GP16 directly to the strip's DIN. Buying, firmware preparation and electrical
  checks belong in helper notes. Later experiments introduce separate LED
  power and signal components when their jobs can be explained.
- Write plainly for a curious learner. Explain a term through the job it does.
  Practical observations should improve an instruction or example; they do not
  automatically belong in the public story as anecdotes or new restrictions.

## Current material and next work

LightVM has twelve programming lessons in Python, JavaScript and Blocks, a
browser preview and runtime inspection tools. The website adds a first hardware
build, electronics experiments and introductions to device communications.
Network activities currently require prepared hardware and developer guides.
There is not yet an equally developed course in all three subjects.

Further educational work should develop the electronics and communications
activities alongside the programming lessons: identifying parts and signals,
measuring a circuit, following messages, and designing what a device does when
a connection is lost. These are curriculum goals, not claims that every sensor,
activity or feature is already supported.

The project is an experimental proof of concept. Describe available software,
firmware downloads and setup requirements accurately. Learning outcomes and
classroom suitability still need evaluation with learners and educators.
Content work does not create a release milestone or change the `0.1.0`
application baseline.
