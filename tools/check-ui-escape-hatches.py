#!/usr/bin/env python3
"""Reject raw UI lifetime escape hatches in production C++.

This complements check-repeated-if-invariants.py.  That checker finds
scattered guard logic; this one enforces the capability boundary: production
code should not grab raw Window LVGL pointers or pointer-like RequiredWindow
handles outside the low-level Window implementation.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
from typing import Iterable


CPP_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
}

SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".cxx"}

DEFAULT_COMPILE_COMMANDS = (
    Path("build/native/compile_commands.json"),
    Path("build/arm-none-eabi/compile_commands.json"),
)

IGNORED_PATH_PARTS = {
    ".git",
    "build",
    "thirdparty",
    "translations",
}

TEST_PATH_PARTS = {
    "tests",
}

LOW_LEVEL_WINDOW_BOUNDARIES = {
    "radio/src/gui/colorlcd/libui/window.cpp",
    "radio/src/gui/colorlcd/libui/window.h",
}

GET_LVOBJ_RE = re.compile(r"\bgetLvObj\s*\(")
REQUIRED_WINDOW_POINTER_API_RE = re.compile(
    r"\b(?:operator\s+T\s*\*|operator\s*->|operator\s*\*)\s*\("
)


class Finding(tuple):
    __slots__ = ()

    def __new__(
        cls,
        path: Path,
        line: int,
        column: int,
        kind: str,
        message: str,
        text: str,
    ):
        return tuple.__new__(cls, (path, line, column, kind, message, text))

    @property
    def path(self) -> Path:
        return self[0]

    @property
    def line(self) -> int:
        return self[1]

    @property
    def column(self) -> int:
        return self[2]

    @property
    def kind(self) -> str:
        return self[3]

    @property
    def message(self) -> str:
        return self[4]

    @property
    def text(self) -> str:
        return self[5]


def repo_root() -> Path:
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        return Path.cwd().resolve()
    return Path(result.stdout.strip()).resolve()


def relpath(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def is_cpp_path(path: Path) -> bool:
    return path.suffix.lower() in CPP_EXTENSIONS


def is_source_path(path: Path) -> bool:
    return path.suffix.lower() in SOURCE_EXTENSIONS


def should_ignore(path: Path, root: Path) -> bool:
    parts = set(relpath(path, root).split("/"))
    return bool(parts & IGNORED_PATH_PARTS)


def is_test_path(path: Path, root: Path) -> bool:
    parts = set(relpath(path, root).split("/"))
    return bool(parts & TEST_PATH_PARTS)


def run_git(args: list[str], root: Path) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=root,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise SystemExit(result.stderr.strip() or "git command failed")
    return result.stdout


def changed_cpp_files(root: Path, base: str) -> set[Path]:
    merge_base = run_git(["merge-base", base, "HEAD"], root).strip()
    output = run_git(["diff", "--name-only", "--diff-filter=ACMRTUXB", merge_base, "HEAD"], root)
    files: set[Path] = set()
    for line in output.splitlines():
        path = (root / line).resolve()
        if path.exists() and is_cpp_path(path) and not should_ignore(path, root):
            files.add(path)
    return files


def collect_cpp_files(root: Path, paths: list[str]) -> set[Path]:
    files: set[Path] = set()
    for raw_path in paths:
        path = (root / raw_path).resolve() if not Path(raw_path).is_absolute() else Path(raw_path).resolve()
        if path.is_file() and is_cpp_path(path) and not should_ignore(path, root):
            files.add(path)
        elif path.is_dir():
            for candidate in path.rglob("*"):
                if candidate.is_file() and is_cpp_path(candidate) and not should_ignore(candidate, root):
                    files.add(candidate.resolve())
        elif not path.exists():
            raise SystemExit(f"path does not exist: {raw_path}")
    return files


def find_compile_commands(root: Path, explicit: str | None) -> Path | None:
    candidates = [Path(explicit)] if explicit else list(DEFAULT_COMPILE_COMMANDS)
    for candidate in candidates:
        path = candidate if candidate.is_absolute() else root / candidate
        if path.exists():
            return path.resolve()
    return None


def resolve_command_path(directory: Path, raw: str) -> Path:
    path = Path(raw)
    if not path.is_absolute():
        path = directory / path
    return path.resolve()


def looks_like_source_arg(arg: str) -> bool:
    if arg.startswith("-"):
        return False
    return Path(arg).suffix.lower() in CPP_EXTENSIONS


def command_args(entry: dict, directory: Path, source_file: Path) -> tuple[str, ...]:
    if "arguments" in entry:
        raw_args = list(entry["arguments"])
    else:
        raw_args = shlex.split(entry["command"])

    args: list[str] = []
    skip_next = False
    source_names = {str(source_file), source_file.name}

    for index, arg in enumerate(raw_args):
        if index == 0:
            continue
        if skip_next:
            skip_next = False
            continue
        if arg in {"-o", "-MF", "-MT", "-MQ"}:
            skip_next = True
            continue
        if arg in {"-c", "-MD", "-MMD", "-MP"}:
            continue
        resolved_arg = resolve_command_path(directory, arg) if looks_like_source_arg(arg) else None
        if arg in source_names or (resolved_arg is not None and resolved_arg == source_file):
            continue
        args.append(arg)

    if not any(arg.startswith("-std=") for arg in args):
        args.append("-std=c++17")
    return tuple(args)


def load_compile_commands(path: Path) -> dict[Path, tuple[Path, tuple[str, ...]]]:
    with path.open(encoding="utf-8") as handle:
        entries = json.load(handle)

    commands: dict[Path, tuple[Path, tuple[str, ...]]] = {}
    for entry in entries:
        directory = Path(entry["directory"]).resolve()
        source_file = resolve_command_path(directory, entry["file"])
        commands[source_file] = (directory, command_args(entry, directory, source_file))
    return commands


def import_clang():
    try:
        from clang import cindex
    except ImportError as exc:
        raise SystemExit(
            "python clang bindings are unavailable. Run through Nix: "
            "nix develop -c python3 tools/check-ui-escape-hatches.py"
        ) from exc

    libclang_path = os.environ.get("LIBCLANG_PATH")
    if libclang_path and not cindex.Config.loaded:
        path = Path(libclang_path)
        if path.is_file():
            cindex.Config.set_library_file(str(path))
        elif path.is_dir():
            cindex.Config.set_library_path(str(path))
    return cindex


def enclosing_roots(paths: Iterable[Path]) -> tuple[Path, ...]:
    roots: set[Path] = set()
    for path in paths:
        roots.add(path if path.is_dir() else path.parent)
    return tuple(sorted(roots))


def under_any(path: Path, roots: Iterable[Path]) -> bool:
    for root in roots:
        try:
            path.relative_to(root)
            return True
        except ValueError:
            pass
    return False


def commands_to_parse(
    selected_files: set[Path],
    commands: dict[Path, tuple[Path, tuple[str, ...]]],
    requested_paths: list[str],
    root: Path,
) -> list[tuple[Path, Path, tuple[str, ...]]]:
    selected_sources = {path for path in selected_files if is_source_path(path) and path in commands}

    if requested_paths:
        requested_roots = enclosing_roots(
            (root / raw).resolve() if not Path(raw).is_absolute() else Path(raw).resolve()
            for raw in requested_paths
        )
        selected_sources.update(
            path for path in commands if under_any(path, requested_roots) and not should_ignore(path, root)
        )

    if not selected_sources:
        selected_sources.update(path for path in selected_files if path in commands)

    return [
        (path, commands[path][0], commands[path][1])
        for path in sorted(selected_sources, key=lambda p: relpath(p, root))
    ]


def regex_findings(path: Path, root: Path) -> list[Finding]:
    if is_test_path(path, root):
        return []

    findings: list[Finding] = []
    relative = relpath(path, root)
    low_level = relative in LOW_LEVEL_WINDOW_BOUNDARIES
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()

    for line_number, line in enumerate(lines, start=1):
        if not low_level:
            match = GET_LVOBJ_RE.search(line)
            if match:
                findings.append(
                    Finding(
                        path,
                        line_number,
                        match.start() + 1,
                        "raw-get-lvobj",
                        "production code must use Window::withLive or a semantic Window method instead of getLvObj()",
                        line.strip(),
                    )
                )
        match = REQUIRED_WINDOW_POINTER_API_RE.search(line)
        if match:
            findings.append(
                Finding(
                    path,
                    line_number,
                    match.start() + 1,
                    "required-window-pointer-api",
                    "RequiredWindow must not expose pointer-like operators",
                    line.strip(),
                )
            )

    return findings


def semantic_lvobj_findings(
    root: Path,
    selected_files: set[Path],
    commands: list[tuple[Path, Path, tuple[str, ...]]],
) -> list[Finding]:
    if not commands:
        return []

    cindex = import_clang()
    index = cindex.Index.create()
    selected = {path.resolve() for path in selected_files if not is_test_path(path, root)}
    findings: list[Finding] = []
    seen: set[tuple[Path, int, int]] = set()

    for source_file, _directory, args in commands:
        translation_unit = index.parse(str(source_file), args=list(args))
        for cursor in translation_unit.cursor.walk_preorder():
            if cursor.kind != cindex.CursorKind.MEMBER_REF_EXPR:
                continue
            if cursor.spelling != "lvobj":
                continue
            location_file = cursor.location.file
            if location_file is None:
                continue
            path = Path(str(location_file)).resolve()
            if path not in selected or relpath(path, root) in LOW_LEVEL_WINDOW_BOUNDARIES:
                continue
            key = (path, cursor.location.line, cursor.location.column)
            if key in seen:
                continue
            seen.add(key)
            line_text = path.read_text(encoding="utf-8", errors="replace").splitlines()[
                cursor.location.line - 1
            ].strip()
            findings.append(
                Finding(
                    path,
                    cursor.location.line,
                    cursor.location.column,
                    "direct-window-lvobj",
                    "production code must not access Window::lvobj directly; use LiveWindow",
                    line_text,
                )
            )

    return findings


def print_findings(findings: list[Finding], root: Path, limit: int) -> None:
    by_kind: dict[str, list[Finding]] = {}
    for finding in findings:
        by_kind.setdefault(finding.kind, []).append(finding)

    print("# UI escape hatch report\n")
    if not findings:
        print("No production UI escape hatches found.")
        return

    total = len(findings)
    print(f"Found {total} production UI escape hatch finding{'s' if total != 1 else ''}.\n")
    for kind in sorted(by_kind):
        records = sorted(by_kind[kind], key=lambda f: (relpath(f.path, root), f.line, f.column))
        print(f"## {kind} ({len(records)})\n")
        for finding in records[:limit]:
            print(f"- {relpath(finding.path, root)}:{finding.line}:{finding.column}: {finding.message}")
            print(f"  `{finding.text}`")
        if len(records) > limit:
            print(f"- ... {len(records) - limit} more")
        print()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="*",
        help="Files or folders to scan. Defaults to C/C++ files changed on this branch.",
    )
    parser.add_argument(
        "--base",
        default="main",
        help="Base branch for default changed-file scan.",
    )
    parser.add_argument(
        "--compile-commands",
        help="Path to compile_commands.json for direct lvobj semantic checks.",
    )
    parser.add_argument(
        "--max-examples",
        type=int,
        default=30,
        help="Maximum examples to print for each finding kind.",
    )
    parser.add_argument(
        "--semantic",
        action="store_true",
        help="Use libclang to also verify direct Window::lvobj member references.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = repo_root()
    os.chdir(root)

    selected_files = (
        collect_cpp_files(root, args.paths)
        if args.paths
        else changed_cpp_files(root, args.base)
    )

    findings: list[Finding] = []
    for path in sorted(selected_files, key=lambda p: relpath(p, root)):
        findings.extend(regex_findings(path, root))

    if args.semantic:
        compile_commands_path = find_compile_commands(root, args.compile_commands)
        if compile_commands_path:
            compile_commands = load_compile_commands(compile_commands_path)
            parse_commands = commands_to_parse(selected_files, compile_commands, args.paths, root)
            findings.extend(semantic_lvobj_findings(root, selected_files, parse_commands))
        else:
            print(
                "warning: compile_commands.json not found; direct Window::lvobj "
                "member-reference checks were skipped",
                file=sys.stderr,
            )

    unique = sorted(
        set(findings),
        key=lambda f: (f.kind, relpath(f.path, root), f.line, f.column),
    )
    print_findings(unique, root, args.max_examples)
    return 1 if unique else 0


if __name__ == "__main__":
    raise SystemExit(main())
