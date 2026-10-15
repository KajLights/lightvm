"""Admission tests independent of the WAMR native executable's scenario."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("wamr_runner", Path(__file__).with_name("run.py"))
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class ManifestTests(unittest.TestCase):
    def setUp(self):
        RUNNER.BUILD.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="wamr-manifest-test-", dir=RUNNER.BUILD)
        self.addCleanup(self.temporary.cleanup)
        self.artifact = Path(self.temporary.name) / "fixture.aot"
        self.artifact.write_bytes(b"\x00aot" + bytes(16))
        self.manifest = dict(schema_version=1, backend="wamr-aot", api_version=1,
                             target="aarch64", pixels=300, buffer_bytes=900,
                             max_instances=3, quantum_pixels=32, max_guest_quantum=64,
                             runtime_heap_budget_bytes=8*1024*1024,
                             trust="generated-local-artifact", sha256=RUNNER.digest(self.artifact))

    def validate(self, manifest=None):
        RUNNER.validate_manifest(self.manifest if manifest is None else manifest, self.artifact, "aarch64")

    def test_valid_profile(self):
        self.validate()

    def test_resource_and_abi_mismatches(self):
        for field, value in {"api_version": 2, "target": "x86_64", "pixels": 301,
                             "max_instances": 4, "buffer_bytes": 901,
                             "quantum_pixels": 99999999,
                             "runtime_heap_budget_bytes": 1}.items():
            with self.subTest(field=field):
                modified = dict(self.manifest, **{field: value})
                with self.assertRaises(ValueError):
                    self.validate(modified)

    def test_boolean_is_not_version_number(self):
        with self.assertRaises(ValueError):
            self.validate(dict(self.manifest, schema_version=True))

    def test_unknown_capability_or_missing_field(self):
        with self.assertRaises(ValueError):
            self.validate(dict(self.manifest, network="unrestricted"))
        modified = copy.deepcopy(self.manifest)
        del modified["trust"]
        with self.assertRaises(ValueError):
            self.validate(modified)

    def test_stale_artifact_digest(self):
        self.artifact.write_bytes(b"\x00aot" + b"changed executable")
        with self.assertRaises(ValueError):
            self.validate()

    def test_wasm_is_not_accepted_as_aot(self):
        self.artifact.write_bytes(b"\x00asm" + bytes(16))
        self.manifest["sha256"] = RUNNER.digest(self.artifact)
        with self.assertRaises(ValueError):
            self.validate()

    def test_manifest_serialization_roundtrip(self):
        self.validate(json.loads(json.dumps(self.manifest)))


if __name__ == "__main__":
    unittest.main()
