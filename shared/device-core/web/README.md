# Browser release-consumer checks

`firmware.mjs` exposes pure candidate assessment, strict RP2040/RP2350 UF2
decoding and bounded SHA-256 payload acquisition. It performs no device writes
and defines no website approval, recommendation or withdrawal policy.
Consumers must validate descriptors/manifests with the shared schemas first.

The same candidate fixtures in `contracts/candidate-cases.json` run against the
Python and browser implementations. These fixtures use CI development metadata;
they are test data, not published releases or approved installation candidates.

Website CI can call `discover_releases.discover(..., on_verified=callback)` to
mirror already-verified bytes while the source directory is available. A
callback failure aborts the import. `release_ids` selects an exact published
inventory for a locked build and fails when a locked release disappears.
The website owns transactional staging, mirroring paths, retention/budgets,
public catalogue policy and the installer experience.
