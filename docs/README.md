# LightVM documentation

Start with the [project README](../README.md) for builds, installation and a first
animation. The guides below describe the current implementation.

## Guides and reference

- [Controllers and development workflow](architecture.md)
- [Animation profile and Python API](animation-profile.md)
- [Native drawing and scene composition](composition.md)
- [A/B scenes and transitions](transitions.md)
- [Python transition profile and optimized helpers](transition-profile.md)
- [PixelArray and user-defined sprites](pixelarray-sprites.md)
- [Motion, easing and strip boundaries](motion.md)
- [Compiler-generated bulk pixel maps](pixel-maps.md)
- [Control API](control-api.md)
- [Device configuration](configuration.md)
- [Networking](networking.md)
- [Boot sequence and power recovery](boot-sequence.md)
- [Browser workspace](web-editor.md)

Runnable [animation examples](../examples/animations/) and
[configuration examples](../examples/config/) live together in `examples/`.
[Scene examples](../examples/scenes/) combine animations from that same directory.

## Future requirements

The [design directory](design/) describes future requirements on top of the
current contract. Superseded implementations are available through Git history.

- [Control API proposal](design/feature-proposal-control-api.md)
- [External inputs for transitions and animations](design/feature-proposal-inputs.md)
- [Device support proposal](design/feature-proposal-device-support.md)
- [Build profiles proposal](design/feature-proposal-build-profiles.md)
- [Web editor proposal](design/feature-proposal-web-editor.md)
- [Animation composition proposal](design/feature-proposal-animation-composition.md)
- [A/B transition design](design/feature-proposal-ab-transitions.md) (implemented; see the guide above)

[Versioning](versioning.md) defines the shared application release and minimum program requirements.
