"""Regression tests for continuation, ownership, admission and package boundaries."""
import ctypes as C
import json
import unittest

import run
from runtime import (
    BUILD, BOUNDS, DONE, PIXELS, READONLY, RENDER, SEED, STALE, STATE_ERROR,
    YIELD, AsyncOutput, Core, Instance, Ledger, Module, Rejected, build,
)


class NativeRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build_info = build()
        cls.core = Core(cls.build_info["core"])

    def setUp(self):
        self.module = Module(self.build_info["manifests"][0])
        self.ledger = Ledger()
        self.instances = []
        self.retired_buffers = []
        self.output = AsyncOutput(self.ledger, self.core, capacity=1)

    def instance(self, mode=0):
        instance = Instance(self.module, self.ledger, mode=mode)
        self.instances.append(instance)
        self.assertEqual(instance.prepare(), DONE)
        return instance

    def tearDown(self):
        self.output.close()
        for instance in self.instances:
            instance.close()
            instance.buffer.dispose_guard()
        for buffer in self.retired_buffers:
            buffer.dispose_guard()
        self.module.close()
        self.assertEqual(self.ledger.current, 0)
        self.assertEqual(self.ledger.allocations, 0)

    def test_continuation_matches_reference_at_multiple_quantums(self):
        instance = self.instance()
        for quantum in (1, 7, 64, 1000000):
            self.assertEqual(instance.render(2, quantum), DONE)
            self.assertEqual(instance.buffer.bytes(), run.reference(2))
        self.assertEqual(instance.state.total_work, 4 * PIXELS)

    def test_partial_and_cancelled_frames_cannot_be_published(self):
        instance = self.instance()
        self.assertEqual(instance.begin(0), YIELD)
        self.assertEqual(instance.step(7), YIELD)
        with self.assertRaises(Rejected):
            self.output.submit(instance.buffer, 0)
        instance.cancel()
        self.assertEqual(instance.step(), STATE_ERROR)
        with self.assertRaises(Rejected):
            self.output.submit(instance.buffer, 0)
        self.assertEqual(self.output.pending, {})

    def test_retention_rotation_and_stale_view_after_release(self):
        instance = self.instance()
        self.assertEqual(instance.render(0), DONE)
        old = instance.buffer
        stale_view = old.view()
        checksum = self.core.hash(old)
        self.output.submit(old, 0, delay=5)
        self.assertEqual(instance.begin(1), READONLY)
        fresh_readonly = old.view()
        self.assertEqual(self.core.lib.core_dim(C.byref(fresh_readonly)), READONLY)
        self.assertEqual(self.core.lib.core_dim(C.byref(stale_view)), STALE)
        self.retired_buffers.append(instance.rotate_buffer())
        self.assertEqual(instance.render(1), DONE)
        self.assertEqual(self.core.hash(old), checksum)
        self.output.poll(4)
        self.assertTrue(old.guard.alive)
        self.output.poll(5)
        self.assertFalse(old.guard.alive)
        self.assertEqual(self.core.lib.core_dim(C.byref(stale_view)), STALE)
        del stale_view, fresh_readonly

    def test_output_cancellation_releases_retained_allocation(self):
        instance = self.instance()
        self.assertEqual(instance.render(0), DONE)
        self.output.submit(instance.buffer, 0)
        instance.close()
        self.assertTrue(instance.buffer.guard.alive)
        self.output.close()
        self.assertFalse(instance.buffer.guard.alive)
        self.assertEqual(self.output.cancelled, 1)

    def test_bounds_failure_is_before_writes(self):
        instance = self.instance()
        instance.begin(0)
        saved = instance.buffer.bytes()
        view = instance.buffer.view()
        view.bytes = 897
        self.assertEqual(instance.step(view=view), BOUNDS)
        self.assertEqual(instance.state.cursor, 0)
        self.assertEqual(instance.buffer.bytes(), saved)
        view.bytes = 899  # malformed RGB8 extent, not just a smaller valid view
        self.assertEqual(instance.step(view=view), BOUNDS)
        del view

    def test_runaway_yields_at_fuel_backedge(self):
        instance = self.instance(mode=1)
        instance.begin(0)
        for _ in range(10):
            previous = instance.state.total_work
            self.assertEqual(instance.step(1000000), YIELD)
            self.assertEqual(instance.state.total_work - previous, 64)
        self.assertEqual(instance.state.phase, RENDER)
        instance.cancel()
        self.assertEqual(instance.step(), STATE_ERROR)

    def test_admission_and_injected_allocation_failures_leave_no_reservation(self):
        before = self.ledger.current
        self.ledger.limit = before + C.sizeof(run.State) + PIXELS * 3
        with self.assertRaises(MemoryError):
            Instance(self.module, self.ledger)
        self.assertEqual(self.ledger.current, before)
        self.assertEqual(self.module.instances, 0)
        self.ledger.limit = 4096
        self.ledger.fail_next = True
        with self.assertRaises(MemoryError):
            Instance(self.module, self.ledger)
        self.assertEqual(self.ledger.current, before)

    def test_manifest_cannot_lie_about_exported_artifact_revision(self):
        manifest = json.loads(self.build_info["manifests"][0].read_text())
        manifest["artifact_revision"] = 2
        path = BUILD / "bad-revision.json"
        path.write_text(json.dumps(manifest))
        with self.assertRaises(Rejected):
            Module(path)

    def test_rejected_initialization_returns_all_reservations(self):
        before = self.ledger.current
        with self.assertRaises(Rejected):
            Instance(self.module, self.ledger, mode=3)
        self.assertEqual(self.ledger.current, before)
        self.assertEqual(self.module.instances, 0)

    def test_malformed_packages_are_rejected(self):
        cases = run.malformed_checks(self.build_info["manifests"][0])
        self.assertTrue(all(cases.values()), cases)

    def test_live_module_cannot_be_unloaded(self):
        self.instance()
        with self.assertRaises(Rejected):
            self.module.close()

    def test_integrated_scenario_and_cleanup(self):
        checks, workload, metrics = run.execute(self.build_info)
        self.assertTrue(all(checks.values()), checks)
        self.assertEqual(workload["frame_hashes"],
                         [f"{run.fnv(run.reference(frame)):08x}" for frame in range(3)])
        self.assertEqual(metrics["native_payload_memory"]["remaining_bytes_after_cleanup"], 0)


if __name__ == "__main__":
    unittest.main()
