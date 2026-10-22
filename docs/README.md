# LightVM documentation

Start with the [project README](../README.md) for builds, installation and a first
animation. The guides below describe the current implementation.

## Guides and reference

- [Controllers and development workflow](architecture.md)
- [Animation profile and Python API](animation-profile.md)
- [Live inputs, timed drivers and wiring](inputs.md)
- [Native drawing and composition](composition.md)
- [A/B compositions and transitions](transitions.md)
- [Python transition profile and optimized helpers](transition-profile.md)
- [PixelArray and user-defined sprites](pixelarray-sprites.md)
- [Motion, easing and strip boundaries](motion.md)
- [Compiler-generated bulk pixel maps](pixel-maps.md)
- [Control API](control-api.md)
- [Controller configuration](configuration.md)
- [Networking](networking.md)
- [Pico 2 W builds, installation and acceptance](pico2-w.md)
- [Multicore rendering and tunable resource budgets](multicore-rendering.md)
- [Multicore hardware measurements and validation](multicore-validation.md)
- [Original Pico W multicore measurements and comparison](multicore-rp2040-validation.md)
- [Multicore design research and decisions](design/feature-proposal-pico-multicore.md)
- [Pico regression review and optimisation priorities](design/pico-performance-review.md)
- [Pico performance and command-lifetime implementation plan](design/pico-performance-plan.md)
- [MQTT, Home Assistant lights and keypad inputs](mqtt.md)
- [Boot sequence and power recovery](boot-sequence.md)
- [Browser workspace](web-editor.md)
- [Generic live music source](audio-source.md)

Runnable [animation examples](../examples/animations/) and
[configuration examples](../examples/config/) live together in `examples/`.
[Composition examples](../examples/compositions/) combine animations from that same directory.

## Future requirements

The [design directory](design/) describes future requirements on top of the
current contract. Superseded implementations are available through Git history.

- [Core numeric types, bulk kernels and native math](design/core-numeric-kernels-plan.md)
- [Numeric foundation contract and standalone verification](numeric-types.md)
- [Independent bytecode emission and integration gates](bytecode-emitter.md)
- [Numeric and kernel workload specification](design/numeric-workloads.md)
- [Control API proposal](design/feature-proposal-control-api.md)
- [External inputs for transitions and animations](design/feature-proposal-inputs.md)
- [ESP32-S3 audio analyser and multicast inputs](design/feature-proposal-audio-multicast.md)
- [Controller support proposal](design/feature-proposal-output-support.md)
- [Build profiles proposal](design/feature-proposal-build-profiles.md)
- [Pico 2 W board support plan](design/feature-proposal-pico2-w.md)
- [Web editor proposal](design/feature-proposal-web-editor.md)
- [Animation composition proposal](design/feature-proposal-animation-composition.md)
- [A/B transition design](design/feature-proposal-ab-transitions.md) (implemented; see the guide above)

[Versioning](versioning.md) defines the shared application release and minimum program requirements.

- [JavaScript and blocks authoring](javascript-and-blocks.md): editor modes, APIs, language coverage and compiler design.

- [Scene preparation and operation lifetime](scene-preparation.md)
- [Pico stack-path accounting](design/pico-stack-accounting.md)
