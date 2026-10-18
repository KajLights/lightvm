# Future browser workspace work

Status: Proposed extensions. The implemented Edit → Preview → Save animation →
Compose → Save preset workflow is documented in [the browser guide](../web-editor.md).
The browser and CLI use the same compiler and current package/protocol contracts.

## Remaining requirements

Keep editable source, current compiled artifacts and pinned animation revisions
in exportable browser storage. Controller programs and presets are deployment data;
active state is RAM. Saving does not activate, and activation does not change
startup. Preserve source backups when refusing obsolete compiled packages.

Future Python, C, blocks or other language frontends must emit the same typed
container and declared effects. Compilation never executes untrusted source.
Do not retain superseded PoC dialects as supported contracts. Any alternative
frontend needs independent numeric, timing and error tests against the core.

A browser preview should use the actual current interpreter or an independent
reference with clearly declared limits. It cannot establish native memory fit,
DMA/RMT timing, electrical output or radio responsiveness. Avoid duplicating
animation logic inside the editor. Canonical examples have one source.

Use capability discovery to offer only available controls. Distinguish physical
controllers, outputs, animation instances, composition layers and future A/B slots.
Future INPUT controls differ from frozen initialization settings. General
event/job controls require the [control model](feature-proposal-control-api.md).

USB and HTTP retain explicit connection identity, boot epoch and mutation
ownership. Never retry an uncertain mutation automatically or silently switch
its transport. Preserve structured errors, revision conflicts, cancellation,
upload ownership and useful progress feedback. Firmware installers package
board-matched images with source revision, hashes and notices.

Bluetooth, Matter, MQTT, hosted builds and additional languages remain separate
features. Each needs discovery, bounded resources, authorization and disconnect
semantics before being offered. Integrate the future
[build profiles](feature-proposal-build-profiles.md),
[controller capabilities](feature-proposal-output-support.md) and
[A/B transitions](feature-proposal-ab-transitions.md) without introducing another
execution contract.
