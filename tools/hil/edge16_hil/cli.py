from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import sys

from . import __version__
from .orchestrator import (
    HilRunner,
    HilSetupError,
    auto_detect_transport,
    exit_code_for_results,
    list_serial_ports,
    make_artifact_dir,
    open_serial_transport,
)
from .reports import load_results_from_trace, write_junit, write_summary


REPO_HIL_ROOT = Path(__file__).resolve().parents[1]
ASSET_ROOT = REPO_HIL_ROOT / "assets"
MODEL_PACK = (
    ("hil-ppm8.yml", "model97.yml", "HIL PPM8"),
    ("hil-sbus16.yml", "model98.yml", "HIL SBUS16"),
    ("hil.yml", "model99.yml", "HIL AUTO"),
)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="edge16-hil")
    parser.add_argument("--version", action="version", version=f"edge16-hil {__version__}")
    subparsers = parser.add_subparsers(dest="command", required=True)

    run_parser = subparsers.add_parser("run", help="run a HIL suite")
    run_parser.add_argument("--suite", default="mvp", choices=["mvp", "ppm-soak"])
    run_parser.add_argument("--device", default="auto", help="serial device path or 'auto'")
    run_parser.add_argument("--baud", default=115200, type=int)
    run_parser.add_argument("--artifact-root", default=Path("build/hil"), type=Path)
    run_parser.add_argument("--connect-timeout", default=6.0, type=float)
    run_parser.add_argument("--profile-timeout", default=3.0, type=float)
    run_parser.add_argument(
        "--duration",
        default="15m",
        help="duration for ppm-soak, e.g. 900s, 15m, or milliseconds as an integer",
    )

    list_parser = subparsers.add_parser("list-devices", help="list serial devices")
    list_parser.add_argument("--json", action="store_true", help="emit JSON")

    prepare_parser = subparsers.add_parser("prepare-sd", help="install radio SD-card assets")
    prepare_parser.add_argument("--mount", required=True, type=Path)
    prepare_parser.add_argument(
        "--auto-model-filename",
        default="model99.yml",
        help="radio-visible filename for the autonomous HIL model",
    )
    prepare_parser.add_argument(
        "--skip-debug-models",
        action="store_true",
        help="install only the autonomous HIL model, not fixed PPM/SBUS debug models",
    )
    prepare_parser.add_argument("--overwrite", action="store_true")
    prepare_parser.add_argument(
        "--skip-models-list-update",
        action="store_true",
        help="do not append the installed model to MODELS/models.yml",
    )

    collect_parser = subparsers.add_parser("collect", help="regenerate reports from trace.jsonl")
    collect_parser.add_argument("--trace", required=True, type=Path)
    collect_parser.add_argument("--suite", default="mvp")
    collect_parser.add_argument("--out", type=Path)

    args = parser.parse_args(argv)
    try:
        if args.command == "run":
            return _cmd_run(args)
        if args.command == "list-devices":
            return _cmd_list_devices(args)
        if args.command == "prepare-sd":
            return _cmd_prepare_sd(args)
        if args.command == "collect":
            return _cmd_collect(args)
    except HilSetupError as exc:
        print(f"edge16-hil: setup error: {exc}", file=sys.stderr)
        return 2
    return 2


def _cmd_run(args: argparse.Namespace) -> int:
    artifact_dir = make_artifact_dir(args.artifact_root)
    if args.device == "auto":
        transport = auto_detect_transport(baud=args.baud, connect_timeout_s=args.connect_timeout)
    else:
        transport = open_serial_transport(
            args.device, baud=args.baud, connect_timeout_s=args.connect_timeout
        )

    try:
        runner = HilRunner(
            transport,
            suite=args.suite,
            artifact_dir=artifact_dir,
            profile_timeout_s=args.profile_timeout,
            soak_ms=_parse_duration_ms(args.duration) if args.suite == "ppm-soak" else None,
        )
        results = runner.run()
    finally:
        transport.close()

    passed = sum(1 for result in results if result.passed)
    failed = len(results) - passed
    print(f"artifacts: {artifact_dir}")
    print(f"result: {passed} passed / {failed} failed")
    return exit_code_for_results(results)


def _cmd_list_devices(args: argparse.Namespace) -> int:
    ports = list_serial_ports()
    if args.json:
        print(json.dumps(ports, indent=2, sort_keys=True))
    else:
        for port in ports:
            desc = f" - {port['description']}" if port["description"] else ""
            hwid = f" [{port['hwid']}]" if port["hwid"] else ""
            print(f"{port['device']}{desc}{hwid}")
    return 0


def _cmd_prepare_sd(args: argparse.Namespace) -> int:
    mount = args.mount
    if not mount.exists() or not mount.is_dir():
        raise HilSetupError(f"mount path does not exist or is not a directory: {mount}")

    installed: list[Path] = []
    _copy_asset(
        ASSET_ROOT / "SCRIPTS/FUNCTIONS/hil.lua",
        mount / "SCRIPTS/FUNCTIONS/hil.lua",
        overwrite=args.overwrite,
        installed=installed,
    )
    model_specs = list(MODEL_PACK)
    if args.skip_debug_models:
        model_specs = [MODEL_PACK[-1]]
    if args.auto_model_filename != "model99.yml":
        model_specs[-1] = ("hil.yml", args.auto_model_filename, "HIL AUTO")

    for asset_name, _, _ in MODEL_PACK:
        _copy_asset(
            ASSET_ROOT / "MODELS" / asset_name,
            mount / "MODELS" / asset_name,
            overwrite=args.overwrite,
            installed=installed,
        )

    models_list = mount / "MODELS/models.yml"
    for asset_name, model_filename, model_name in model_specs:
        _copy_asset(
            ASSET_ROOT / "MODELS" / asset_name,
            mount / "MODELS" / model_filename,
            overwrite=args.overwrite,
            installed=installed,
        )
        if not args.skip_models_list_update:
            if _ensure_models_list_entry(models_list, model_filename, model_name):
                if models_list not in installed:
                    installed.append(models_list)

    for path in installed:
        print(path)
    return 0


def _copy_asset(src: Path, dst: Path, *, overwrite: bool, installed: list[Path]) -> None:
    if not src.exists():
        raise HilSetupError(f"missing bundled asset: {src}")
    if dst.exists() and not overwrite:
        raise HilSetupError(f"{dst} already exists; pass --overwrite to replace it")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    installed.append(dst)


def _ensure_models_list_entry(path: Path, model_filename: str, model_name: str) -> bool:
    entry = (
        f"  {model_filename}:\n"
        f"    hash: \"\"\n"
        f"    name: \"{model_name}\"\n"
        f"    labels: \"\"\n"
        f"    lastopen: 0\n"
    )
    if path.exists():
        text = path.read_text(encoding="utf-8", errors="replace")
        if f"{model_filename}:" in text:
            return False
        if "Models:" not in text:
            text = text.rstrip() + "\n\nModels:\n"
        elif not text.endswith("\n"):
            text += "\n"
        path.write_text(text.rstrip() + "\n" + entry, encoding="utf-8")
        return True

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("Models:\n" + entry, encoding="utf-8")
    return True


def _cmd_collect(args: argparse.Namespace) -> int:
    trace = args.trace
    if not trace.exists():
        raise HilSetupError(f"trace does not exist: {trace}")
    out = args.out or trace.parent
    out.mkdir(parents=True, exist_ok=True)
    results = load_results_from_trace(trace)
    if not results:
        raise HilSetupError(f"no case_result records in {trace}")
    write_summary(out / "summary.md", suite=args.suite, results=results, trace_path=trace)
    write_junit(out / "results.xml", suite=args.suite, results=results)
    print(f"wrote {out / 'summary.md'}")
    print(f"wrote {out / 'results.xml'}")
    return exit_code_for_results(results)


def _parse_duration_ms(value: str) -> int:
    text = value.strip().lower()
    if not text:
        raise HilSetupError("duration cannot be empty")
    scale = 1
    number = text
    if text.endswith("ms"):
        number = text[:-2]
        scale = 1
    elif text.endswith("s"):
        number = text[:-1]
        scale = 1000
    elif text.endswith("m"):
        number = text[:-1]
        scale = 60 * 1000
    try:
        parsed = float(number)
    except ValueError as exc:
        raise HilSetupError(f"invalid duration: {value}") from exc
    duration_ms = int(parsed * scale)
    if duration_ms < 1000:
        raise HilSetupError("duration must be at least 1s")
    if duration_ms > 60 * 60 * 1000:
        raise HilSetupError("duration must be at most 60m")
    return duration_ms


if __name__ == "__main__":
    raise SystemExit(main())
