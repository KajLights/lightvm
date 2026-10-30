# Project instructions

## Experimental proof of concept

LightVM is an experimental PoC, not a released product. There are no users; only
test devices run it. Their storage can be wiped and the software reinstalled.
We do not need to preserve every previous experiment.

- Prefer a clean current design over backward compatibility with earlier experiments.
- Breaking changes to experimental APIs, protocols, configuration and storage formats are acceptable.
- Do not add or retain compatibility shims, migrations, fallback implementations or obsolete code paths solely to support previous experiments.
- When persisted experimental state becomes incompatible, document the wipe/reprovision or clean-install steps instead of implementing a migration.
- Update tests and documentation to reflect the current design; remove expectations that exist only to preserve abandoned experiments.

## Milestone-only application versioning

- The current application version and compatibility baseline are `0.1.0`, declared in `lightvm/release.json`. All currently implemented features belong to this baseline.
- Keep the application version unchanged during development. Individual features, fixes, refactors, experiments and commits must not cause version bumps or introduce new feature-version floors.
- Advance the version only for an explicitly agreed project milestone or when Kaj directly requests a version change. Completing a feature is not itself a release milestone.
- Identify development builds with Git commit/build metadata or the installer's `--build-id`, rather than advancing the application version.
- When a milestone changes compatibility, update the shared version, compiler and firmware admission checks, editor checks, tests and documentation together. Do not invent compatibility requirements for unreleased intermediate experiments.
- Do not create release tags or publish releases unless explicitly requested. Version `1.0.0` requires an explicitly agreed release milestone; it must not be inferred from feature completion.
