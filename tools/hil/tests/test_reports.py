from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

from edge16_hil.reports import (
    CaseResult,
    load_results_from_trace,
    result_to_trace_record,
    write_junit,
    write_summary,
)


class ReportTests(unittest.TestCase):
    def test_junit_generation_marks_failures(self) -> None:
        results = [
            CaseResult("profile_ppm_lock", "pass", duration_ms=100),
            CaseResult("ppm_identity", "fail", "mismatch", duration_ms=250),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "results.xml"
            write_junit(path, suite="mvp", results=results)
            root = ET.parse(path).getroot()
            self.assertEqual(root.attrib["tests"], "2")
            self.assertEqual(root.attrib["failures"], "1")
            self.assertEqual(root.findall("testcase")[1].find("failure").attrib["message"], "mismatch")

    def test_summary_contains_results(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "summary.md"
            trace = Path(tmp) / "trace.jsonl"
            write_summary(
                path,
                suite="mvp",
                results=[CaseResult("sbus_identity", "pass", metrics={"frames": "42"})],
                trace_path=trace,
            )
            text = path.read_text(encoding="utf-8")
            self.assertIn("sbus_identity", text)
            self.assertIn("frames", text)

    def test_load_results_from_trace(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            trace = Path(tmp) / "trace.jsonl"
            records = [
                {"type": "serial", "line": "READY fw=x"},
                result_to_trace_record(CaseResult("profile_roundtrip", "pass")),
            ]
            trace.write_text("\n".join(json.dumps(record) for record in records) + "\n", encoding="utf-8")
            results = load_results_from_trace(trace)
            self.assertEqual(len(results), 1)
            self.assertEqual(results[0].name, "profile_roundtrip")


if __name__ == "__main__":
    unittest.main()
