# Composition extensions

The implemented [scene contract](../composition.md) uses independent animation/1
instances, premultiplied RGBA contributions and native over/add/screen/multiply
composition. Stars over Color or Rainbow reuses the same foreground program.
Black paint is opaque; unwritten pixels are transparent. Instances own settings,
state, clocks and RNG; the compositor owns order, opacity and physical output.

Future filters/readback must specify which completed frame is visible, its
lifetime, color format, rounding and fault/publication boundary. Effects that
need history should own explicit simulation data. Do not infer transparency
from black or add implicit retained paint.

[A/B transitions](feature-proposal-ab-transitions.md) consume completed scenes.
Their mix position and movement lifecycle differ from per-layer alpha. Admission
must account for both scenes, layer state, working/committed contributions,
filter inputs, transition resources and immutable physical output buffers.
Neither a maximum layer count nor a minimal mix-buffer estimate proves fit.

Verify blend endpoints, independent clocks, candidate failure, paused layers,
output cancellation, no partial publication and no allocation during rendering.
Current host and C contracts provide the baseline; hardware throughput remains a
separate measurement. Follow the
[provenance rules](feature-proposal-control-api.md#prior-art-and-provenance).
