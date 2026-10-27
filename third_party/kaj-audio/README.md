# Pinned kaj-fi audio source snapshot

`source.tar.gz` is a deterministic export of the exact Git blobs listed in
`manifest.json`, from the revision in `kaj-audio.rev`. Kaj-fi remains the source
of truth; do not edit extracted files or maintain a separate detector here.

The snapshot includes the portable core, WASM build script, independent PCM
fixtures and the S3 FFT adapter/ESP-DSP subset needed for conformance. ESP-DSP's
upstream metadata and notices remain in `components/esp-dsp/`. This internal
source packaging does not change upstream ownership or licensing.

The kaj-fi repository is private. Bundling the pinned sources allows ordinary CI
and offline builds without a cross-repository credential. Run:

```sh
python tools/fetch_audio_core.py
```

This checks the archive and per-file SHA-256 digests, then materializes
`.deps/kaj-fi`. An existing modified directory is rejected and left intact.
CMake, the WASM builder and all parity harnesses also check the consumed files
against the manifest. A separately selected `KAJ_AUDIO_CORE` checkout is allowed
only when those exact source bytes match, regardless of unrelated Git commits.

To update, commit the upstream change in kaj-fi, then deliberately export it:

```sh
python tools/update_audio_snapshot.py --repository /path/to/kaj-fi --revision FULL_40_DIGIT_COMMIT
```

Review the upstream diff, manifest and pin together. Preserve any local dependency
edits before removing/reprovisioning `.deps/kaj-fi`; there is no automatic migration
or overwrite. Rebuild both firmware consumers and the WASM module, then rerun
the analysis and transport acceptance checks appropriate to the upstream change.
