# Firmware release production and discovery

Product builds produce firmware; the website owns release selection and the
public installation experience. Ordinary source commits and Pages deployments
do not create releases or replace a public firmware catalogue. LightVM remains
at application and compatibility version **0.1.0**. No tag or release is created
by implementing this workflow.

## Producer contract

`release/targets.json` registers the approved matrix, exact physical targets,
nominal flash geometry, required/excluded features, content/tool contracts and
notice inputs. LightVM initially produces standard builds for Pico, Pico W,
Pico 2 W and the XIAO ESP32 C3/S3/C6, plus MQTT/HA variants for the five wireless
boards. These combinations already have native CI coverage. Standard excludes
MQTT/HA and UDP audio subscriptions. The currently linked command audio gateway
is honestly reported; fully removing audio code is a separate profile task.
Unvalidated audio and OTA profiles are not offered as released combinations.

The shared producer in `shared/device-core/tools/release.py` has three commands:

```sh
python shared/device-core/tools/release.py prepare
python shared/device-core/tools/release.py package \
  --target lightvm-pico_w-standard --build build/pico-w --output build/bundle
python shared/device-core/tools/release.py assemble \
  --bundles build/bundles --output build/distribution --public
```

Install the pinned Python dependencies from `requirements.txt` first. Each
command reads the product's registered targets; a package cannot invent a
profile. The CLI verifies the application's authoritative version. `prepare`
without a tag and `assemble` without `--tag` produce development evidence only.

Each build writes an intermediate `artifact.json` and flat, namespaced assets.
Assembly requires exactly the registered matrix, one product/version/source
commit, no duplicate asset names, and matching asset bytes and digests. The
result is canonical, compact ASCII JSON plus a newline in `release.json` and a
`SHA256SUMS` file. The manifest is bounded to 1 MiB and rejects duplicate JSON
members, unsafe paths and unknown document members. Asset names contain no paths.

The schemas reference the single build/firmware definitions by stable URNs;
the validator registers these local resources and never fetches schema URLs.
The release embeds each exact `kajlights.build` record. The packager recomputes
the input and feature digests and finds the same generated firmware/features
strings inside the linked payload. Same-version builds and profiles therefore
retain separate build IDs. The embedded release label is itself a build-ID input.
ESP defaults enable the SDK's reproducible mode to omit build dates and paths.
Payload SHA-256 values bind the actual distributed
bytes without trying to embed a binary's own complete digest into itself.

ESP packages contain bootloader, binary partition table and app, with offsets
from `flasher_args.json`. Binary table MD5, nonoverlap, flash bounds, a single
factory app, storage region, segment checksum, image SHA-256, chip IDs and chip
revision limits are checked. App size is checked against the actual table, not
an assumed 2 MiB. These are complete local-install parts, not OTA payloads.

Pico packages contain the validated UF2, compiled storage geometry and linked
RAM/stack evidence. Family IDs, block addresses/order, RP2350 workaround blocks,
boot-journal space, content boundaries and the stack report's ELF digest are
checked. UF2 file bytes are distinct from flashed firmware bytes. Resource
constants are compiled into an **unlinked object**, so overrides and board
defaults use the actual compiler without consuming firmware space. Pico's VM
budget comes from linked RAM accounting; ESP's effective VM budget is runtime
dependent and is reported as unknown, rather than as a physical measurement.

All manifests mark physical-install acceptance and OTA as unvalidated. They
require export and clean local installation; no content-preservation claim or
migration is inferred from equal application versions or layout labels. Notices
include product inputs and the shared services' MIT licence. Compiler/preview
tool bundles are not yet published; a tool contract identifies what the website
must provide before opening normal product controls.

## GitHub workflow and deliberate releases

`.github/workflows/release.yml` validates relevant pull requests and supports a
manual run from any selected branch through `workflow_dispatch` once the workflow
exists on the default branch. It builds and packages the approved matrix
and uploads Actions evidence. A branch validation never creates a GitHub Release
and is never a stable catalogue candidate.

A deliberately pushed `v<product-version>` tag, or a manual run selected on that
existing tag, additionally prepares an **unpublished draft**. The exact tag must
match the version source, resolve to HEAD, have a clean checkout, be reachable
from `origin/main`, and already have a successful `build.yml` run for that exact
commit. LightVM reads `lightvm/release.json`; kaj-fi reads its existing
`PROJECT_VER` without changing it. Tags with a prerelease version produce a
prerelease draft. Workflow concurrency serializes the same ref. Existing
releases/assets cause failure; there is no clobber, automatic tag creation,
publication or repository-wide latest recommendation.

For a public repository the workflow signs `release.json` with GitHub's OIDC
build provenance and retains `release.sigstore.json` with the draft. Its signed
manifest binds every payload hash. This uses no repository signing-key secret.
These attestations are website-CI provenance; they do not implement the future
device-side signed OTA contract. Publication, immutable-release settings,
physical acceptance and website recommendation remain deliberate release work.

kaj-fi's source repository is private. Its workflow builds the registered PDM
and synthetic S3 variants at the current 16-band default, and retains private
packages/drafts without publishing private provenance into public Sigstore.
The public discovery tool rejects private distribution. No public kaj-fi binary
route or repository-visibility change is introduced here.

## Website discovery

The reusable verification client runs during website CI, not in every visitor's
browser:

```sh
python shared/device-core/tools/discover_releases.py \
  --repository KajLights/lightvm --output build/verified-releases.json
```

It uses GitHub CLI read access, enumerates every release page, explicitly drops
drafts even for privileged credentials, and excludes prereleases unless opted
in. A tag alone, development record or release without `release.json` is not a
candidate. An empty published list produces an honest empty result. API errors
fail acquisition and do not replace an existing output with an empty catalogue.

For each candidate it resolves lightweight/annotated tags to commits, validates
the schema and product/tag/source identities, then verifies the Sigstore bundle
against the allowlisted repository, exact release workflow, source tag/commit
and GitHub-hosted runner identity. Every listed asset is downloaded and checked
against its signed size/hash. Private sources, missing provenance/assets,
moved tags, channel disagreement, conflicting identities and partial acquisition
fail the import. The output records release IDs and publication URLs; the
website must add its own approved distribution mirror, import lock, withdrawal
and recommendation policy before hosting assets or enabling installation.

`release_contract.assess` is a pure preliminary candidate check over a fresh
device description, artifact, requested features, installation method and
available tool contracts. It distinguishes installed builds, incompatible
targets/features/methods, missing physical flash/revision probes, matching tools
and clean local installation. It uses actual measured flash, not the installed
firmware's nominal declaration; it never selects by nickname or version alone.
It does not perform a device write, claim content compatibility, automatically
downgrade, or admit OTA. Full website content/preflight policy follows separately.

## Verification and remaining gates

`tests/test_release_distribution.py` covers complete bundles, native image/table
integrity, wrong chips/builds, missing payloads, size and identity conflicts,
same-version builds, feature/method/tool constraints, physical-geometry probes,
matrix completeness, malicious paths/JSON, release pagination and draft/channel
filtering. Existing UF2 tests exercise all three Pico families and malformed
payloads. Real cross-build packaging is exercised by the validation workflow.

No public release has been created by this work, so live signature verification
and a physical install from a published catalogue remain first-release gates.
The website UI and asset mirroring must consume these contracts before the
current installer can offer release selection. Pages now builds the workspace
without development firmware images; the local developer packager remains
available for explicit test builds.

References: [GitHub release API](https://docs.github.com/en/rest/releases/releases#list-releases),
[draft-release CLI](https://cli.github.com/manual/gh_release_create),
[GitHub attest action](https://github.com/actions/attest),
[attestation verification](https://cli.github.com/manual/gh_attestation_verify).
