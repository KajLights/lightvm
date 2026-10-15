"""Regression tests for the independent comparison gate, not backend internals."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("component_comparison", Path(__file__).with_name("run.py"))
comparison = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(comparison)


class ComparisonTests(unittest.TestCase):
    def valid(self):
        return {
            "schema_version": 1, "backend": "direct-native", "status": "passed",
            "environment": {}, "workload": comparison.expected_workload(),
            "checks": dict.fromkeys(comparison.COMMON_CHECKS, True),
            "metrics": {}, "limitations": ["host only"],
        }

    def test_fnv_known_vectors(self):
        self.assertEqual(comparison.fnv1a(b""), "811c9dc5")
        self.assertEqual(comparison.fnv1a(b"a"), "e40c292c")
        self.assertEqual(comparison.fnv1a(b"foobar"), "bf9cf968")

    def test_reference_frame_lengths_and_state(self):
        self.assertEqual(comparison.reference_frame(0, 0, 0), b"")
        # Zero seed's first xorshift remains zero, then adds the fixed constant.
        self.assertEqual(comparison.reference_frame(1, 0, 0), bytes((0xF5, 0x79, 0x2B)))
        self.assertEqual(len(comparison.reference_frame(300, comparison.SEED, 0)), 900)
        self.assertNotEqual(comparison.reference_frame(300, comparison.SEED, 0),
                            comparison.reference_frame(300, comparison.SEED, 1))

    def test_accepts_complete_evidence(self):
        self.assertEqual(comparison.validate_result(self.valid(), "native"), [])

    def test_rejects_missing_common_check(self):
        for name in comparison.COMMON_CHECKS:
            with self.subTest(name=name):
                value = self.valid()
                del value["checks"][name]
                self.assertTrue(comparison.validate_result(value, "native"))

    def test_rejects_truthy_non_boolean_checks(self):
        for wrong in (1, "true", [True], None, False):
            with self.subTest(wrong=wrong):
                value = self.valid()
                value["checks"]["resumable_execution"] = wrong
                self.assertTrue(comparison.validate_result(value, "native"))

    def test_rejects_numerically_wrong_frames(self):
        value = self.valid()
        value["workload"]["frame_hashes"][1] = "00000000"
        errors = comparison.validate_result(value, "native")
        self.assertTrue(any("frame_hashes" in error for error in errors))

    def test_rejects_wrong_backend_and_workload(self):
        value = self.valid()
        value["backend"] = "typed-interpreter"
        value["workload"]["pixels"] = 100
        self.assertEqual(len(comparison.validate_result(value, "native")), 2)

    def test_cannot_count_unavailable_as_pass(self):
        value = self.valid()
        value["status"] = "unavailable"
        self.assertTrue(comparison.validate_result(value, "native"))

    def test_rejects_malformed_envelope(self):
        for value in ([], None, "passed", {}):
            with self.subTest(value=value):
                self.assertTrue(comparison.validate_result(value, "native"))
        for key, bad in (("schema_version", True), ("metrics", []),
                         ("environment", None), ("limitations", "none")):
            with self.subTest(key=key):
                value = copy.deepcopy(self.valid())
                value[key] = bad
                self.assertTrue(comparison.validate_result(value, "native"))

    def test_additional_failed_check_invalidates_pass(self):
        value = self.valid()
        value["checks"]["allocation_cleanup"] = False
        self.assertTrue(comparison.validate_result(value, "native"))

    def execute_fixture(self, source, timeout=5):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "native").mkdir()
            (root / "native/run.py").write_text(source)
            results = root / "results"
            results.mkdir()
            with patch.object(comparison, "HERE", root), patch.object(comparison, "ROOT", root):
                return comparison.execute_backend("native", results, timeout)

    def fixture_writer(self, value, code=0):
        return ("import json, pathlib, sys\n"
                f"value = json.loads({json.dumps(value)!r})\n"
                "pathlib.Path(sys.argv[2]).write_text(json.dumps(value))\n"
                f"raise SystemExit({code})\n")

    def test_process_exit_failure_overrides_passing_json(self):
        result = self.execute_fixture(self.fixture_writer(self.valid(), code=7))
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["returncode"], 7)

    def test_process_success_without_evidence_fails(self):
        result = self.execute_fixture("print('everything passed')\n")
        self.assertEqual(result["status"], "failed")
        self.assertIn("no readable result", result["errors"][0])

    def test_available_and_unavailable_are_distinct(self):
        result = self.execute_fixture(self.fixture_writer(self.valid()))
        self.assertEqual(result["status"], "passed")
        value = self.valid()
        value["status"] = "unavailable"
        result = self.execute_fixture(self.fixture_writer(value, code=2))
        self.assertEqual(result["status"], "unavailable")

    def test_timeout_does_not_accept_partial_success(self):
        source = self.fixture_writer(self.valid()).replace("raise SystemExit(0)",
                                                          "import time; time.sleep(10)")
        result = self.execute_fixture(source, timeout=0.05)
        self.assertEqual(result["status"], "failed")
        self.assertIn("timed out", result["errors"][0])


if __name__ == "__main__":
    unittest.main()
