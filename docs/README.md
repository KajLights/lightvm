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

- [Control API proposal](design/feature-proposal-control-api.md)
- [External inputs for transitions and animations](design/feature-proposal-inputs.md)
- [ESP32-S3 audio analyser and multicast inputs](design/feature-proposal-audio-multicast.md)
- [Controller support proposal](design/feature-proposal-output-support.md)
- [Build profiles proposal](design/feature-proposal-build-profiles.md)
- [Web editor proposal](design/feature-proposal-web-editor.md)
- [Animation composition proposal](design/feature-proposal-animation-composition.md)
- [A/B transition design](design/feature-proposal-ab-transitions.md) (implemented; see the guide above)

[Versioning](versioning.md) defines the shared application release and minimum program requirements.

- [JavaScript and blocks authoring](javascript-and-blocks.md): editor modes, APIs, language coverage and compiler design.
