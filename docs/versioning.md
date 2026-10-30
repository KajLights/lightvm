# Application versions and program requirements

LightVM is an experimental proof of concept with no users, only test devices.
The current application version and compatibility baseline are both `0.1.0`,
stored in `lightvm/release.json`. The compiler, firmware build, editor, browser
preview and installer manifests use that shared version. Every currently
implemented feature belongs to the `0.1.0` baseline.

## Milestone-only versioning

Keep `0.1.0` throughout current development. Features, fixes, refactors,
experiments and commits do not create versions or new feature-version floors.
Advance the application version only for an explicitly agreed project milestone
or when Kaj directly requests a version change. Reaching `1.0.0`, creating release
tags and publishing releases each require an explicit decision.

Identify builds using Git commit/build metadata, such as
`0.1.0+commit.abc123`, or the installer's separate `--build-id`. Build metadata
records provenance and does not change compatibility. Builds within this
experimental milestone can change semantics; a matching version is not a claim
that every intermediate test build supports every later experiment. Rebuild the
compiler/editor and firmware from the same source when testing new functionality.

At an agreed future milestone, update the shared version, compatibility checks,
tests and documentation together. Introduce feature floors only when they
represent actual supported milestone boundaries. Do not assign versions to
unreleased intermediate features. Breaking experimental changes are acceptable;
recompile or reprovision test devices instead of adding compatibility shims or
migrations. See [AGENTS.md](../AGENTS.md).

## Program manifests

A program manifest uses an unversioned document kind:

```json
{
  "schema": "lightvm.program",
  "compiler_version": "0.1.0+commit.abc123",
  "requires": "0.1.0",
  "profile": "animation",
  "pixels": {"format": "rgba8-premul"},
  "settings": []
}
```

`compiler_version` records provenance. It never sets the minimum firmware version.
`requires` declares a minimum application version within the same major. The
ordering rule supports future milestones; it does not promise compatibility
between arbitrary experimental builds or future breaking milestones.

Full firmware/compiler versions use three numeric components. Requirements also
accept major or major.minor shorthand, normalized by filling missing components
with zero. For current programs, `requires("0.1")` means `0.1.0`. `requires("0")`
means `0.0.0` and is rejected because it is below the supported baseline.

| Program requirement | Controller version | Result |
| --- | --- | --- |
| `0.1` or `0.1.0` | `0.1.0` | Accepted |
| `0.1.0` | `0.1.0+commit.abc123` | Accepted |
| `0.1.1` or `0.2` | `0.1.0` | Rejected: requires a newer version |
| `0.1.0` | `0.0.9` or `0.1.0-rc.1` | Rejected: controller is older |
| `1` or an old experimental `1.x` requirement | `0.1.0` | Rejected: different major |
| `0`, `0.0.9` or `0.1.0-rc.1` | `0.1.0` | Rejected: below the supported baseline |

Missing or malformed requirements and missing controller versions fail checks.
Numeric components are bounded to unsigned 32-bit values; version strings are
at most 95 ASCII characters. Prereleases follow SemVer ordering, with numeric
identifiers compared numerically. Build metadata does not affect ordering.

## Authoring and admission

```python
from lightvm import profile, requires, rgb, render
profile("animation")
requires("0.1.0")  # Optional: all current source defaults to this baseline.

@render
def draw(ctx, runtime, pixels):
    pixels.fill(rgb(255, 64, 0))
```

One optional top-level `requires(...)` declaration is accepted. Omitting it
selects `0.1.0` for every current feature: drawing, controls, fixed values/arrays,
bitwise operations, prepared kernels, native math and Perlin/simplex noise.
Explicit requirements below the baseline or in another major are rejected.
A declaration can request a future version; execution then requires firmware
that meets it. Advancing compiler provenance alone never raises program requirements.

Firmware admission checks the required version against both the controller and
the supported baseline for every package, including generic typed callbacks.
There are no separate version modes for earlier experiments. Type signatures,
effect permissions, resource identities, native bindings and reserved encodings
remain independently validated. Invalid programs cannot bypass those checks by
claiming a compatible version.

Firmware `describe` and `STATUS` report `version`. Target-aware Python compilation
checks it and the module limit. The editor compiles independently of the connected
controller so source editing and external saves remain available. Preview/upload
checks compatibility; firmware checks admission before activation. Rejection
preserves the running animation and its generation. Optional services use
capability booleans such as `live_inputs`, `light_controls`, `transition_programs`,
`compositions`, `presets` and `connections`; resource limits are reported separately.

Document kinds (`lightvm.program`, `lightvm.config`, `lightvm.network`,
`lightvm.composition`, `lightvm.preset`, `lightvm.source`, `lightvm.discovery`)
identify data without independent version counters. Bytecode layout markers,
binding IDs, management frames, HTTP routes and storage directory markers
identify physical encodings; they are not separately negotiated product versions.

## Resetting earlier experiments

The earlier `1.0.0` through `1.4.0` labels were unreleased experimental build
labels, not supported application releases. They are superseded by this `0.1.0`
baseline. Dated validation reports retain their original labels, source commits
and hashes as historical evidence; their feature floors are no longer policy.

Rebuild the editor/browser preview and firmware from current source. Recompile
saved Python, JavaScript or Blocks source to regenerate `.lbc` programs with
`requires: 0.1.0`, then upload them again. Old `1.x` packages are rejected; do not
rewrite only their manifest labels. Replace stale hosted editor/installer assets
with current build output when deploying.

For a clean test-device installation, preserve any source or configuration you
still want outside the device, wipe/reprovision its experimental storage using the
board's provisioning procedure, flash current firmware, then reapply configuration
and recompile/reupload programs and presets. No migration or old-version runtime
is provided. A storage wipe is needed only when the persisted state is incompatible
or a clean installation is desired; changing the application label does not itself
change the bytecode or storage layout.
