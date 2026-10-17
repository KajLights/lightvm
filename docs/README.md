# LightVM documentation

Start with the [project README](../README.md) for builds, installation and a first
animation. The guides below describe the current implementation.

## Guides and reference

- [Architecture and native ports](architecture.md)
- [Animation profile and Python API](animation-profile.md)
- [Native drawing and scene composition](composition.md)
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

## Design and history

The [design directory](design/) collects decisions, requirements, proposals and
reviews. These record the direction and context at the time they were written;
their status notes identify proposed, deferred or superseded work. Use the guides
above for current behavior.

- [Portable bytecode architecture decision](design/architecture-decision-portable-bytecode.md)
- [Component runtime requirements](design/architecture-request-component-runtime.md)
- [Control API proposal](design/feature-proposal-control-api.md)
- [Device support proposal](design/feature-proposal-device-support.md)
- [Build profiles proposal](design/feature-proposal-build-profiles.md)
- [Web editor proposal](design/feature-proposal-web-editor.md)
- [Animation composition proposal](design/feature-proposal-animation-composition.md)
- [A/B transitions proposal](design/feature-proposal-ab-transitions.md)
- [Persistent animation state proposal](design/feature-proposal-persistent-animation-state.md)
- [Picolights animation coverage review](design/review-picolights-animation-coverage.md)

Historical implementation and measurement records remain with their code in
[`experiments/`](../experiments/).
