# LightVM documentation

Start with the [project README](../README.md) for builds, installation and a first
animation. The guides below describe the current implementation.

## Guides and reference

- [Learn with lights](teaching.md): twelve beginner lessons, local preview and clear syntax.
- [Teaching implementation and acceptance](design/teaching-implementation.md)
- [Beginner observation session](teaching-pilot.md)
- [Controllers and development workflow](architecture.md)
- [Animation profile and Python API](animation-profile.md)
- [Live inputs, timed drivers and wiring](inputs.md)
- [Board status and native sensor inputs](board-status.md)
- [Native drawing and composition](composition.md)
- [A/B compositions and transitions](transitions.md)
- [Python transition profile and optimized helpers](transition-profile.md)
- [PixelArray and user-defined sprites](pixelarray-sprites.md)
- [Motion, easing and strip boundaries](motion.md)
- [Seeded gradient noise](noise.md): native Perlin/simplex, live inputs and field selection.
- [Compiler-generated bulk pixel maps](pixel-maps.md)
- [Control API](control-api.md)
- [Controller configuration](configuration.md)
- [Networking](networking.md)
- [Pico 2 W builds, installation and acceptance](pico2-w.md)
- [Multicore rendering and tunable resource budgets](multicore-rendering.md)
- [Optimisation protocol, regression gates and historical baselines](optimisation-protocol.md)
- [Multicore hardware measurements and validation](multicore-validation.md)
- [Original Pico W multicore measurements and comparison](multicore-rp2040-validation.md)
- [Multicore design research and decisions](design/feature-proposal-pico-multicore.md)
- [Pico regression review and optimisation priorities](design/pico-performance-review.md)
- [Pico performance and command-lifetime implementation plan](design/pico-performance-plan.md)
- [C3 default execution profile and Pico hardware comparison](design/c3-performance-2026-10-24.md)
- [ESP32 optimisation campaign and thermal validation](design/esp32-optimisation-2026-10-24.md)
- [ESP32 noise performance and live input measurements](design/noise-esp32-2026-10-24.md)
- [MQTT, Home Assistant lights and keypad inputs](mqtt.md)
- [Boot sequence and power recovery](boot-sequence.md)
- [Browser workspace](web-editor.md)
- [Generic live music source](audio-source.md)
- [Audio subscriptions, source editing and Python visualizations](audio-subscriptions.md)
- [Structured audio gateway](audio-gateway.md)
- [Shared browser audio analysis](audio-analysis-browser.md)
- [Audio acceptance evidence](audio-acceptance.md)
- [Audio protocol review, OSC comparison and follow-up findings](audio-protocol-review-2026-10-28.md)

Runnable [animation examples](../examples/animations/) and
[configuration examples](../examples/config/) live together in `examples/`.
[Composition examples](../examples/compositions/) combine animations from that same directory.

## Future requirements

The [design directory](design/) describes future requirements on top of the
current contract. Superseded implementations are available through Git history.

- [Beginner teaching, language and bytecode review](design/teaching-language-review-2026-10-27.md):
  proposed twelve-lesson course, syntax across Python/JavaScript/Blocks, measured
  usability gaps and compiler/VM priorities for short strips and rings.

- [Core numeric types, bulk kernels and native math](design/core-numeric-kernels-plan.md)
- [Numeric foundation contract and standalone verification](numeric-types.md)
- [Independent bytecode emission and integration gates](bytecode-emitter.md)
- [Numeric and kernel workload specification](design/numeric-workloads.md)
- [Control API proposal](design/feature-proposal-control-api.md)
- [External inputs for transitions and animations](design/feature-proposal-inputs.md)
- [Original multicast audio experiment](design/feature-proposal-audio-multicast.md) (historical; superseded by subscriptions)
- [Controller support proposal](design/feature-proposal-output-support.md)
- [Build profiles proposal](design/feature-proposal-build-profiles.md)
- [OTA firmware updates and build discovery](design/feature-proposal-firmware-ota.md):
  hosted upgrades/downgrades, optional Pico W OTA, per-board recovery and shared
  installed-build/feature reporting.
- [Pico 2 W board support plan](design/feature-proposal-pico2-w.md)
- [Web editor proposal](design/feature-proposal-web-editor.md)
- [Animation composition proposal](design/feature-proposal-animation-composition.md)
- [A/B transition design](design/feature-proposal-ab-transitions.md) (implemented; see the guide above)

[Versioning](versioning.md) defines the shared application release and minimum program requirements.

- [JavaScript and blocks authoring](javascript-and-blocks.md): editor modes, APIs, language coverage and compiler design.

- [Scene preparation and operation lifetime](scene-preparation.md)
- [Pico stack-path accounting](design/pico-stack-accounting.md)
