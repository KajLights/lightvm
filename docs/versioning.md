# Application releases and program requirements

LightVM has one public application version, stored in `lightvm/release.json`.
The compiler, firmware build, editor and installer manifests read that same file.
The initial compatibility baseline is `1.0.0`; this does not create a Git tag or
publish a release.

A program manifest uses an unversioned document kind:

```json
{
  "schema": "lightvm.program",
  "compiler_version": "1.4.0+commit.abc123",
  "requires": "1.0.0",
  "profile": "animation",
  "pixels": {"format": "rgba8-premul"},
  "settings": []
}
```

`compiler_version` records provenance. It never sets the minimum firmware release.
`requires` declares the minimum application release within that major:

| Requirement | Accepted releases | Rejected examples |
| --- | --- | --- |
| `1` | 1.0.0 and later 1.x.x | 0.9.0, 2.0.0 |
| `1.2` | 1.2.0 and later 1.x.x | 1.1.9, 2.0.0 |
| `1.2.3` | 1.2.3 and later 1.x.x | 1.2.2, 2.0.0 |

Missing or malformed requirements and missing controller versions fail compatibility
checks. Full firmware/compiler versions use three numeric components; program
requirements also accept the shorthand forms above. Numeric components are
bounded to unsigned 32-bit values; version strings are at most 95 ASCII characters.
Build metadata (`+commit.abc123`) does not affect ordering. Prereleases sort before
the corresponding stable release, with numeric prerelease identifiers compared
numerically. Thus `1.2.0-rc.2` does not satisfy `1.2`, but a stable `1.2.0` satisfies
a minimum of `1.2.0-rc.2`.

## Authoring and feature floors

```python
from lightvm import profile, requires, rgb, render
profile("animation")
requires("1")

@render
def draw(ctx, runtime, pixels):
    pixels.fill(rgb(255, 64, 0))
```

The compiler normalizes `requires("1.2")` to `1.2.0`. One optional top-level
`requires(...)` declaration is accepted. Omitting it selects the source's feature
floor: `1.1.0` for an `Input(..., control=...)` declaration, otherwise `1.0.0`.
Requirements below that floor or in another major are rejected. A declaration may request a future
minor release; it prevents execution on older firmware even when the source uses
only baseline operations.

When a future release adds a feature, its compiler path must raise the program's
minimum only when that feature is emitted. An explicit requirement below the
feature's introduction release must produce a diagnostic. Updating the compiler's
own version must never raise existing programs' requirements. Do not add another
schema, profile, drawing API or interpreter revision for such additions.

Firmware `describe` and `STATUS` report `version`. Target-aware Python compilation
checks that version and the module limit. The editor compiles independently of
the connected controller, so source editing, download and external file saves remain
available. Preview/upload checks the controller release; firmware independently checks
admission before activation. A rejected package leaves the running animation and
its generation intact. Optional services use capability booleans such as `compositions`,
`presets`, and `connections`, with resource limits reported separately.

Editor imports `.py` and `lightvm.source` files without requiring a controller.
Source remains editable and exportable when the connected firmware cannot execute
it. Preview and deployment check current compilation and release compatibility.
The abandoned browser library and its compiled versions are no longer loaded or
migrated; recover Python from external backups and recompile.

## Development and releases

Keep the upcoming application version throughout development. Use build metadata
or the installer's separate `--build-id` commit identifier to identify builds;
individual experiments do not need version bumps. Minor releases add compatible
features. Patch releases preserve the source and execution contract. Breaking
changes to released features require a major release, with the affected behaviour
and minimum release documented here or in release notes. No legacy interpreter,
automatic runtime adapter or per-release execution switch is implied.

Document kinds (`lightvm.program`, `lightvm.config`, `lightvm.network`,
`lightvm.composition`, `lightvm.preset`, `lightvm.source`, `lightvm.discovery`) identify
the kind of data, without independent version counters. The `animation` source
profile is a fixed authoring name.

The bytecode magic/layout marker, binding IDs, management frame marker, HTTP route
and storage directory marker identify physical encodings. They stay fixed across
compatible application releases and are checked to avoid interpreting invalid
bytes. They are implementation details, not separately negotiated product
versions. Unknown instructions, invalid permissions and retired native bindings
remain rejected even if a package claims a compatible release.

## Pre-baseline cutover

Earlier schema-numbered artifacts and retired experiments are outside the initial
1.0.0 contract. Recompile supported source with `profile("animation")` to produce a
release-based package. Retired runtimes and operations remain removed. Saved
configuration/network/composition documents use the unversioned document kinds above;
there is no automatic conversion of old firmware storage. Back up data before
explicitly reprovisioning a controller with an unsupported storage layout. Later
compatible releases must preserve supported 1.x behaviour and readable data,
rather than repeat this cutover.
