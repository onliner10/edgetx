from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from edge16_hil.orchestrator import CaseSpec, HilRunner


class FakeTransport:
    port = "fake"

    def __init__(self, lines: list[str] | None = None):
        self.lines = list(lines or [])
        self.writes: list[str] = []

    def write_line(self, line: str) -> None:
        self.writes.append(line)

    def read_line(self, timeout_s: float) -> str | None:
        if self.lines:
            return self.lines.pop(0)
        return None

    def close(self) -> None:
        pass


class OrchestratorTests(unittest.TestCase):
    def make_runner(self, transport: FakeTransport) -> HilRunner:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        return HilRunner(
            transport,
            suite="mvp",
            artifact_dir=Path(tmp.name),
            profile_timeout_s=0.01,
            command_timeout_s=0.01,
            case_grace_s=0.01,
        )

    def make_soak_runner(self, transport: FakeTransport) -> HilRunner:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        return HilRunner(
            transport,
            suite="ppm-soak",
            artifact_dir=Path(tmp.name),
            profile_timeout_s=0.01,
            command_timeout_s=0.01,
            case_grace_s=0.01,
            soak_ms=1000,
        )

    def test_missing_lock_fails_profile_case(self) -> None:
        transport = FakeTransport(["PROFILE profile=ppm8 seq=1 status=sent"])
        runner = self.make_runner(transport)
        result = runner._set_profile("ppm8", "profile_ppm_lock")
        self.assertEqual(result.status, "fail")
        self.assertIn("did not lock", result.message)

    def test_failed_case_result_is_reported(self) -> None:
        transport = FakeTransport(
            ["RESULT case=ppm_identity status=fail message=mismatch frames=8"]
        )
        runner = self.make_runner(transport)
        result = runner._run_case(CaseSpec("ppm_identity", "ppm8", 0))
        self.assertEqual(result.status, "fail")
        self.assertEqual(result.message, "mismatch")
        self.assertEqual(result.metrics["frames"], "8")

    def test_pc_fails_case_when_metrics_show_mismatch(self) -> None:
        transport = FakeTransport(
            ["RESULT case=ppm_identity status=pass frames=8 mismatches=1 stale=0"]
        )
        runner = self.make_runner(transport)
        result = runner._run_case(CaseSpec("ppm_identity", "ppm8", 0))
        self.assertEqual(result.status, "fail")
        self.assertIn("mismatches", result.message)

    def test_malformed_line_is_skipped(self) -> None:
        transport = FakeTransport(
            [
                "SAMPLE missing_equals",
                "RESULT case=ppm_identity status=pass frames=9",
            ]
        )
        runner = self.make_runner(transport)
        result = runner._run_case(CaseSpec("ppm_identity", "ppm8", 0))
        self.assertEqual(result.status, "pass")

    def test_case_timeout_fails(self) -> None:
        transport = FakeTransport([])
        runner = self.make_runner(transport)
        result = runner._run_case(CaseSpec("ppm_identity", "ppm8", 0))
        self.assertEqual(result.status, "fail")
        self.assertIn("timed out", result.message)

    def test_ppm_soak_runs_after_ppm_lock(self) -> None:
        transport = FakeTransport(
            [
                "PROFILE profile=ppm8 seq=1 status=sent",
                "LOCK profile=ppm8 seq=1 frames=5 stale_other=0",
                (
                    "RESULT case=ppm_soak status=pass seq=2 frames=40 mismatches=0 "
                    "missed=0 stale=0 malformed=0 frozen=0"
                ),
            ]
        )
        runner = self.make_soak_runner(transport)
        results = runner._run_ppm_soak()
        self.assertEqual([result.name for result in results], ["profile_ppm_lock", "ppm_soak"])
        self.assertTrue(all(result.passed for result in results))
        self.assertIn("RUN case=ppm_soak ms=1000 seq=2", transport.writes)

    def test_ppm_soak_fails_on_frozen_metric(self) -> None:
        transport = FakeTransport(
            ["RESULT case=ppm_soak status=pass seq=1 frames=40 mismatches=0 frozen=1"]
        )
        runner = self.make_soak_runner(transport)
        result = runner._run_case(CaseSpec("ppm_soak", "ppm8", 1000))
        self.assertEqual(result.status, "fail")
        self.assertIn("frozen", result.message)

    def test_ppm_soak_ignores_channel_mismatch_metric(self) -> None:
        transport = FakeTransport(
            [
                (
                    "RESULT case=ppm_soak status=pass seq=1 frames=40 mismatches=12 "
                    "missed=0 stale=0 malformed=0 frozen=0"
                )
            ]
        )
        runner = self.make_soak_runner(transport)
        result = runner._run_case(CaseSpec("ppm_soak", "ppm8", 1000))
        self.assertEqual(result.status, "pass")


if __name__ == "__main__":
    unittest.main()
