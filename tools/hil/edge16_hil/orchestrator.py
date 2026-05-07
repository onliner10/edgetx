from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
import json
from pathlib import Path
import time
from typing import Callable, Iterable

from .protocol import ProtocolError, UnoEvent, format_command, parse_event_line
from .reports import CaseResult, result_to_trace_record, write_junit, write_summary


class HilSetupError(RuntimeError):
    pass


@dataclass(frozen=True)
class CaseSpec:
    name: str
    profile: str
    run_ms: int


MVP_CASES: tuple[CaseSpec, ...] = (
    CaseSpec("ppm_identity", "ppm8", 2600),
    CaseSpec("ppm_loss_recovery", "ppm8", 2600),
    CaseSpec("sbus_identity", "sbus16", 2600),
)

PPM_SOAK_CASE = CaseSpec("ppm_soak", "ppm8", 15 * 60 * 1000)


class SerialTransport:
    def __init__(self, serial_obj):
        self._serial = serial_obj

    @property
    def port(self) -> str:
        return str(getattr(self._serial, "port", "unknown"))

    def close(self) -> None:
        self._serial.close()

    def write_line(self, line: str) -> None:
        self._serial.write((line + "\n").encode("ascii"))
        self._serial.flush()

    def read_line(self, timeout_s: float) -> str | None:
        deadline = time.monotonic() + timeout_s
        old_timeout = getattr(self._serial, "timeout", None)
        try:
            while time.monotonic() < deadline:
                remaining = max(0.01, min(0.20, deadline - time.monotonic()))
                self._serial.timeout = remaining
                raw = self._serial.readline()
                if not raw:
                    continue
                return raw.decode("ascii", errors="replace").strip()
        finally:
            self._serial.timeout = old_timeout
        return None


def _import_serial():
    try:
        import serial
        import serial.tools.list_ports
    except ImportError as exc:
        raise HilSetupError("pyserial is required; run through nix develop or install pyserial") from exc
    return serial


def list_serial_ports() -> list[dict[str, str]]:
    serial = _import_serial()
    ports = []
    for port in serial.tools.list_ports.comports():
        ports.append(
            {
                "device": port.device,
                "description": port.description or "",
                "hwid": port.hwid or "",
                "vid": f"{port.vid:04x}" if port.vid is not None else "",
                "pid": f"{port.pid:04x}" if port.pid is not None else "",
                "serial_number": port.serial_number or "",
            }
        )
    return ports


def open_serial_transport(device: str, *, baud: int, connect_timeout_s: float) -> SerialTransport:
    serial = _import_serial()
    try:
        ser = serial.Serial(device, baudrate=baud, timeout=0.1, write_timeout=1.0)
    except serial.SerialException as exc:
        raise HilSetupError(f"failed to open {device}: {exc}") from exc
    transport = SerialTransport(ser)
    try:
        _probe_ready(transport, connect_timeout_s=connect_timeout_s)
    except Exception:
        transport.close()
        raise
    return transport


def auto_detect_transport(*, baud: int, connect_timeout_s: float) -> SerialTransport:
    errors: list[str] = []
    for port in list_serial_ports():
        device = port["device"]
        try:
            return open_serial_transport(device, baud=baud, connect_timeout_s=connect_timeout_s)
        except HilSetupError as exc:
            errors.append(f"{device}: {exc}")
    details = "; ".join(errors) if errors else "no serial devices found"
    raise HilSetupError(f"could not auto-detect Edge16 HIL Uno ({details})")


def _probe_ready(transport: SerialTransport, *, connect_timeout_s: float) -> UnoEvent:
    deadline = time.monotonic() + connect_timeout_s
    seq = 0
    last_error = "no response"
    while time.monotonic() < deadline:
        try:
            transport.write_line(format_command("PING", seq=seq))
        except Exception as exc:
            raise HilSetupError(f"failed to write PING to {transport.port}: {exc}") from exc
        seq += 1
        line = transport.read_line(0.35)
        if line is None:
            continue
        try:
            event = parse_event_line(line)
        except ProtocolError as exc:
            last_error = str(exc)
            continue
        if event.kind == "READY":
            return event
        last_error = f"unexpected {event.kind}"
    raise HilSetupError(f"{transport.port} did not answer READY ({last_error})")


class HilRunner:
    def __init__(
        self,
        transport: SerialTransport,
        *,
        suite: str,
        artifact_dir: Path,
        profile_timeout_s: float = 3.0,
        command_timeout_s: float = 2.0,
        case_grace_s: float = 2.0,
        soak_ms: int | None = None,
    ):
        self.transport = transport
        self.suite = suite
        self.artifact_dir = artifact_dir
        self.profile_timeout_s = profile_timeout_s
        self.command_timeout_s = command_timeout_s
        self.case_grace_s = case_grace_s
        self.soak_ms = soak_ms
        self.seq = 1
        self.trace_path = artifact_dir / "trace.jsonl"
        self._trace_handle = None

    def run(self) -> list[CaseResult]:
        self.artifact_dir.mkdir(parents=True, exist_ok=True)
        results: list[CaseResult] = []
        with self.trace_path.open("w", encoding="utf-8") as trace:
            self._trace_handle = trace
            self._trace({"type": "session", "suite": self.suite, "port": self.transport.port})
            self._selftest()

            if self.suite == "ppm-soak":
                results.extend(self._run_ppm_soak())
            else:
                results.extend(self._run_mvp())
            self._send("STOP")

            for result in results:
                self._trace(result_to_trace_record(result))
            self._trace({"type": "session_end"})
            self._trace_handle = None

        write_summary(
            self.artifact_dir / "summary.md",
            suite=self.suite,
            results=results,
            trace_path=self.trace_path,
        )
        write_junit(self.artifact_dir / "results.xml", suite=self.suite, results=results)
        return results

    def _run_mvp(self) -> list[CaseResult]:
        results: list[CaseResult] = []
        results.append(self._set_profile("ppm8", "profile_ppm_lock"))
        if results[-1].passed:
            for spec in MVP_CASES:
                if spec.profile == "ppm8":
                    results.append(self._run_case(spec))
        else:
            results.append(_skipped("ppm_identity", "PPM profile did not lock"))
            results.append(_skipped("ppm_loss_recovery", "PPM profile did not lock"))

        results.append(self._set_profile("sbus16", "profile_sbus_lock"))
        if results[-1].passed:
            for spec in MVP_CASES:
                if spec.profile == "sbus16":
                    results.append(self._run_case(spec))
        else:
            results.append(_skipped("sbus_identity", "SBUS profile did not lock"))

        results.append(self._set_profile("ppm8", "profile_roundtrip"))
        return results

    def _run_ppm_soak(self) -> list[CaseResult]:
        results = [self._set_profile("ppm8", "profile_ppm_lock")]
        if not results[-1].passed:
            results.append(_skipped("ppm_soak", "PPM profile did not lock"))
            return results
        soak = CaseSpec("ppm_soak", "ppm8", self.soak_ms or PPM_SOAK_CASE.run_ms)
        results.append(self._run_case(soak))
        return results

    def _selftest(self) -> None:
        started = time.monotonic()
        self._send("SELFTEST", seq=self._next_seq())
        event = self._wait_for(
            lambda ev: ev.kind == "RESULT" and ev.fields.get("case") == "selftest",
            timeout_s=self.command_timeout_s,
        )
        duration_ms = int((time.monotonic() - started) * 1000)
        if event is None:
            raise HilSetupError("SELFTEST timed out")
        if event.kind == "ERROR":
            raise HilSetupError(event.fields.get("message", "SELFTEST command failed"))
        if event.fields.get("status") != "pass":
            message = event.fields.get("message", "SELFTEST failed")
            raise HilSetupError(message)
        self._trace({"type": "selftest", "duration_ms": duration_ms, "fields": event.fields})

    def _set_profile(self, profile: str, result_name: str) -> CaseResult:
        started = time.monotonic()
        seq = self._next_seq()
        self._send("SET", profile=profile, seq=seq)
        profile_seen = False

        def accept(event: UnoEvent) -> bool:
            nonlocal profile_seen
            if event.kind == "PROFILE" and event.fields.get("profile") == profile:
                profile_seen = True
            return event.kind == "LOCK" and event.fields.get("profile") == profile

        event = self._wait_for(accept, timeout_s=self.profile_timeout_s)
        duration_ms = int((time.monotonic() - started) * 1000)
        if event is None:
            message = f"{profile} module output did not lock within {self.profile_timeout_s:.1f}s"
            if not profile_seen:
                message += "; Uno did not acknowledge profile command"
            return CaseResult(result_name, "fail", message, duration_ms)
        if event.kind == "ERROR":
            return CaseResult(
                result_name,
                "fail",
                event.fields.get("message", "profile command failed"),
                duration_ms,
                event.fields,
            )

        metrics = {k: v for k, v in event.fields.items() if k not in {"profile", "seq"}}
        if result_name == "profile_roundtrip" and int(metrics.get("stale_other", "0")) > 0:
            return CaseResult(
                result_name,
                "fail",
                "stale frames from previous protocol observed during roundtrip",
                duration_ms,
                metrics,
            )
        return CaseResult(result_name, "pass", "", duration_ms, metrics)

    def _run_case(self, spec: CaseSpec) -> CaseResult:
        started = time.monotonic()
        seq = self._next_seq()
        self._send("RUN", case=spec.name, ms=spec.run_ms, seq=seq)
        timeout_s = max(self.command_timeout_s, spec.run_ms / 1000.0 + self.case_grace_s)
        event = self._wait_for(
            lambda ev: ev.kind == "RESULT" and ev.fields.get("case") == spec.name,
            timeout_s=timeout_s,
        )
        duration_ms = int((time.monotonic() - started) * 1000)
        if event is None:
            return CaseResult(spec.name, "fail", f"case timed out after {timeout_s:.1f}s", duration_ms)
        if event.kind == "ERROR":
            return CaseResult(
                spec.name,
                "fail",
                event.fields.get("message", "case command failed"),
                duration_ms,
                event.fields,
            )

        status = "pass" if event.fields.get("status") == "pass" else "fail"
        message = event.fields.get("message", "")
        metrics = {
            key: value
            for key, value in event.fields.items()
            if key not in {"case", "status", "message", "seq"}
        }
        if spec.name != "ppm_soak" and _metric_int(metrics, "mismatches") != 0:
            status = "fail"
            message = message or "channel mismatches observed"
        if _metric_int(metrics, "stale") != 0:
            status = "fail"
            message = message or "stale frames from another protocol observed"
        if _metric_int(metrics, "missed") != 0:
            status = "fail"
            message = message or "missed output periods observed"
        if _metric_int(metrics, "malformed") != 0:
            status = "fail"
            message = message or "malformed PPM frames observed"
        if _metric_int(metrics, "frozen") != 0:
            status = "fail"
            message = message or "frozen output detected"
        return CaseResult(spec.name, status, message, duration_ms, metrics)

    def _send(self, command: str, **fields: object) -> None:
        line = format_command(command, **fields)
        self.transport.write_line(line)
        self._trace({"type": "serial", "direction": "tx", "line": line})

    def _wait_for(self, predicate: Callable[[UnoEvent], bool], *, timeout_s: float) -> UnoEvent | None:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            line = self.transport.read_line(max(0.01, deadline - time.monotonic()))
            if line is None:
                continue
            record: dict[str, object] = {"type": "serial", "direction": "rx", "line": line}
            try:
                event = parse_event_line(line)
            except ProtocolError as exc:
                record["parse_error"] = str(exc)
                self._trace(record)
                continue
            record["event"] = {"kind": event.kind, "fields": event.fields}
            self._trace(record)
            if event.kind == "ERROR":
                return event
            if predicate(event):
                return event
        return None

    def _next_seq(self) -> int:
        value = self.seq
        self.seq = (self.seq + 1) & 0x7FFF
        if self.seq == 0:
            self.seq = 1
        return value

    def _trace(self, record: dict[str, object]) -> None:
        if self._trace_handle is None:
            return
        record.setdefault("ts", datetime.now(timezone.utc).isoformat())
        self._trace_handle.write(json.dumps(record, sort_keys=True) + "\n")
        self._trace_handle.flush()


def _skipped(name: str, message: str) -> CaseResult:
    return CaseResult(name=name, status="fail", message=f"skipped: {message}")


def _metric_int(metrics: dict[str, str], name: str) -> int:
    try:
        return int(metrics.get(name, "0"), 0)
    except ValueError:
        return 0


def make_artifact_dir(root: Path) -> Path:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    candidate = root / stamp
    suffix = 1
    while candidate.exists():
        candidate = root / f"{stamp}-{suffix}"
        suffix += 1
    return candidate


def exit_code_for_results(results: Iterable[CaseResult]) -> int:
    return 0 if all(result.passed for result in results) else 1
