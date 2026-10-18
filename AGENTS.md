# Project instructions

## Experimental proof of concept

LightVM is an experimental PoC, not a released product. There is currently one
device running it for testing, and its storage can be wiped and the software
reinstalled. We do not need to preserve every previous experiment.

- Prefer a clean current design over backward compatibility with earlier experiments.
- Breaking changes to experimental APIs, protocols, configuration and storage formats are acceptable.
- Do not add or retain compatibility shims, migrations, fallback implementations or obsolete code paths solely to support previous experiments.
- When persisted experimental state becomes incompatible, document the wipe/reprovision or clean-install steps instead of implementing a migration.
- Update tests and documentation to reflect the current design; remove expectations that exist only to preserve abandoned experiments.
