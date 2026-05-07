from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
import json
from pathlib import Path
import xml.etree.ElementTree as ET


@dataclass
class CaseResult:
    name: str
    status: str
    message: str = ""
    duration_ms: int = 0
    metrics: dict[str, str] = field(default_factory=dict)

    @property
    def passed(self) -> bool:
        return self.status == "pass"


def result_from_dict(data: dict[str, object]) -> CaseResult:
    metrics_raw = data.get("metrics", {})
    metrics = {str(k): str(v) for k, v in dict(metrics_raw).items()}
    return CaseResult(
        name=str(data.get("name", "")),
        status=str(data.get("status", "fail")),
        message=str(data.get("message", "")),
        duration_ms=int(data.get("duration_ms", 0)),
        metrics=metrics,
    )


def result_to_trace_record(result: CaseResult) -> dict[str, object]:
    return {"type": "case_result", "result": asdict(result)}


def load_results_from_trace(trace_path: Path) -> list[CaseResult]:
    results: list[CaseResult] = []
    with trace_path.open("r", encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            record = json.loads(line)
            if record.get("type") == "case_result":
                results.append(result_from_dict(dict(record["result"])))
    return results


def write_summary(path: Path, *, suite: str, results: list[CaseResult], trace_path: Path) -> None:
    passed = sum(1 for result in results if result.passed)
    failed = len(results) - passed
    now = datetime.now(timezone.utc).isoformat()

    lines = [
        f"# Edge16 HIL Summary",
        "",
        f"- Suite: `{suite}`",
        f"- Generated: `{now}`",
        f"- Result: `{passed} passed / {failed} failed`",
        f"- Trace: `{trace_path}`",
        "",
        "| Case | Status | Duration | Message |",
        "| --- | --- | ---: | --- |",
    ]
    for result in results:
        message = result.message.replace("|", "\\|")
        lines.append(
            f"| `{result.name}` | `{result.status}` | {result.duration_ms} ms | {message} |"
        )

    metric_lines: list[str] = []
    for result in results:
        if not result.metrics:
            continue
        metric_lines.append(f"## {result.name}")
        metric_lines.append("")
        for key in sorted(result.metrics):
            metric_lines.append(f"- `{key}`: `{result.metrics[key]}`")
        metric_lines.append("")
    if metric_lines:
        lines.extend(["", *metric_lines])

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_junit(path: Path, *, suite: str, results: list[CaseResult]) -> None:
    failures = sum(1 for result in results if not result.passed)
    testsuite = ET.Element(
        "testsuite",
        {
            "name": f"edge16-hil.{suite}",
            "tests": str(len(results)),
            "failures": str(failures),
            "errors": "0",
            "time": f"{sum(r.duration_ms for r in results) / 1000.0:.3f}",
        },
    )
    for result in results:
        testcase = ET.SubElement(
            testsuite,
            "testcase",
            {
                "classname": f"edge16_hil.{suite}",
                "name": result.name,
                "time": f"{result.duration_ms / 1000.0:.3f}",
            },
        )
        if not result.passed:
            failure = ET.SubElement(
                testcase,
                "failure",
                {
                    "message": result.message or result.status,
                    "type": "HilFailure",
                },
            )
            failure.text = result.message or f"{result.name} failed"
        if result.metrics:
            props = ET.SubElement(testcase, "properties")
            for key in sorted(result.metrics):
                ET.SubElement(props, "property", {"name": key, "value": result.metrics[key]})

    tree = ET.ElementTree(testsuite)
    ET.indent(tree, space="  ")
    tree.write(path, encoding="utf-8", xml_declaration=True)
