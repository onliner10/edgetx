#!/usr/bin/env python3
"""Report repeated invariants hidden in C/C++ if conditions.

The checker uses libclang to find real `if` statements, then groups repeated
whole conditions, repeated predicate atoms, and broader semantic families.  It
intentionally does not restrict itself to early-return guards: the body action
is reported as context, because repeated predicates around side effects are
also scattered invariants.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
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
HEADER_EXTENSIONS = {".h", ".hh", ".hpp", ".hxx"}

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

PREDICATE_CALL_PREFIXES = (
    "accept",
    "can",
    "deleted",
    "enabled",
    "empty",
    "has",
    "is",
    "live",
    "valid",
)

WINDOW_LIFETIME_RECEIVERS = {
    "body",
    "button",
    "child",
    "choice",
    "content",
    "dialog",
    "field",
    "form",
    "header",
    "keyboard",
    "list",
    "menu",
    "numberedit",
    "overlay",
    "page",
    "parent",
    "quickmenu",
    "self",
    "slider",
    "table",
    "textedit",
    "this",
    "w",
    "widget",
    "window",
}

WINDOW_LIFETIME_BOUNDARY_SUFFIXES = (
    "radio/src/gui/colorlcd/LvglWrapper.cpp",
    "radio/src/gui/colorlcd/libui/etx_lv_theme.cpp",
    "radio/src/gui/colorlcd/libui/mainwindow.cpp",
    "radio/src/gui/colorlcd/libui/list_line_button.cpp",
    "radio/src/gui/colorlcd/libui/list_line_button.h",
    "radio/src/gui/colorlcd/libui/window.cpp",
    "radio/src/gui/colorlcd/libui/window.h",
    "radio/src/lua/lua_lvgl_widget.h",
)

INVARIANT_BOUNDARY_CALLS = (
    "fromAvailableLvObj",
    "initRequiredLvObj",
    "initRequiredWindow",
    "markLoaded",
    "requireLvObj",
    "runWhenLoaded",
    "withLive",
)

KEYWORDS = {
    "alignof",
    "catch",
    "decltype",
    "delete",
    "do",
    "else",
    "false",
    "for",
    "if",
    "new",
    "nullptr",
    "return",
    "sizeof",
    "static_cast",
    "switch",
    "true",
    "while",
}

INTERESTING_SUBSTRINGS = (
    "acceptsevents",
    "content",
    "deleted",
    "enabled",
    "has",
    "isavailable",
    "lv_obj",
    "lvobj",
    "nullptr",
    "valid",
)

UI_SAFETY_FAMILIES = {
    "LVGL object presence/lifetime",
    "Window availability/event liveness",
    "Content pointer presence",
}

UI_SAFETY_VARIABLES = {
    "body",
    "box",
    "button",
    "child",
    "choice",
    "content",
    "dialog",
    "field",
    "form",
    "header",
    "keyboard",
    "label",
    "line",
    "list",
    "lvobj",
    "menu",
    "overlay",
    "page",
    "parent",
    "slider",
    "table",
    "textedit",
    "toolbar",
    "w",
    "widget",
    "window",
}

UI_SAFETY_CALLS = {
    "acceptsEvents",
    "hasLiveLvObj",
    "isAvailable",
    "isKeyboardReady",
}

UI_SAFETY_ATOM_SUBSTRINGS = (
    "acceptsevents",
    "deleted",
    "getlvobj",
    "haslivelvobj",
    "isavailable",
    "iskeyboardready",
    "loaded",
    "lv_obj",
    "lvobj",
)


@dataclasses.dataclass(frozen=True)
class CompileCommand:
    file: Path
    directory: Path
    args: tuple[str, ...]


@dataclasses.dataclass(frozen=True)
class IfRecord:
    file: Path
    line: int
    column: int
    condition: str
    normalized_condition: str
    atoms: tuple[str, ...]
    call_keys: tuple[str, ...]
    variable_keys: tuple[str, ...]
    family_keys: tuple[str, ...]
    body_action: str
    body_calls: tuple[str, ...]
    function: str
    scope: str


@dataclasses.dataclass(frozen=True)
class Group:
    key: str
    records: tuple[IfRecord, ...]

    @property
    def files(self) -> tuple[Path, ...]:
        return tuple(sorted({record.file for record in self.records}))

    @property
    def count(self) -> int:
        return len(self.records)


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


def is_header_path(path: Path) -> bool:
    return path.suffix.lower() in HEADER_EXTENSIONS


def should_ignore(path: Path, root: Path) -> bool:
    parts = set(relpath(path, root).split("/"))
    return bool(parts & IGNORED_PATH_PARTS)


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


def find_compile_commands(root: Path, explicit: str | None) -> Path:
    candidates = [Path(explicit)] if explicit else list(DEFAULT_COMPILE_COMMANDS)
    for candidate in candidates:
        path = candidate if candidate.is_absolute() else root / candidate
        if path.exists():
            return path.resolve()
    wanted = ", ".join(str(path) for path in candidates)
    raise SystemExit(
        "compile_commands.json not found. Build first, for example: "
        "nix develop -c tools/ui-harness/edgetx-ui build tx16s "
        f"(looked for {wanted})"
    )


def resolve_command_path(directory: Path, raw: str) -> Path:
    path = Path(raw)
    if not path.is_absolute():
        path = directory / path
    return path.resolve()


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


def looks_like_source_arg(arg: str) -> bool:
    if arg.startswith("-"):
        return False
    return Path(arg).suffix.lower() in CPP_EXTENSIONS


def load_compile_commands(path: Path) -> dict[Path, CompileCommand]:
    with path.open(encoding="utf-8") as handle:
        entries = json.load(handle)

    commands: dict[Path, CompileCommand] = {}
    for entry in entries:
        directory = Path(entry["directory"]).resolve()
        source_file = resolve_command_path(directory, entry["file"])
        commands[source_file] = CompileCommand(
            file=source_file,
            directory=directory,
            args=command_args(entry, directory, source_file),
        )
    return commands


def import_clang():
    try:
        from clang import cindex
    except ImportError as exc:
        raise SystemExit(
            "python clang bindings are unavailable. Run through Nix: "
            "nix develop -c python3 tools/check-repeated-if-invariants.py"
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
    commands: dict[Path, CompileCommand],
    requested_paths: list[str],
    root: Path,
) -> list[CompileCommand]:
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

    return [commands[path] for path in sorted(selected_sources, key=lambda p: relpath(p, root))]


def nearest_compile_command(header: Path, commands: dict[Path, CompileCommand]) -> CompileCommand | None:
    best: tuple[int, CompileCommand] | None = None
    header_parts = header.parts
    for command in commands.values():
        common = 0
        for left, right in zip(header_parts, command.file.parts):
            if left != right:
                break
            common += 1
        if best is None or common > best[0]:
            best = (common, command)
    return best[1] if best else None


def source_bytes(path: Path, cache: dict[Path, bytes]) -> bytes:
    if path not in cache:
        cache[path] = path.read_bytes()
    return cache[path]


def find_matching_paren(data: bytes, open_index: int) -> int | None:
    depth = 0
    index = open_index
    in_line_comment = False
    in_block_comment = False
    in_string = False
    in_char = False
    escaped = False

    while index < len(data):
        char = data[index]
        next_char = data[index + 1] if index + 1 < len(data) else None

        if in_line_comment:
            if char == ord("\n"):
                in_line_comment = False
            index += 1
            continue
        if in_block_comment:
            if char == ord("*") and next_char == ord("/"):
                in_block_comment = False
                index += 2
            else:
                index += 1
            continue
        if in_string:
            if escaped:
                escaped = False
            elif char == ord("\\"):
                escaped = True
            elif char == ord('"'):
                in_string = False
            index += 1
            continue
        if in_char:
            if escaped:
                escaped = False
            elif char == ord("\\"):
                escaped = True
            elif char == ord("'"):
                in_char = False
            index += 1
            continue

        if char == ord("/") and next_char == ord("/"):
            in_line_comment = True
            index += 2
            continue
        if char == ord("/") and next_char == ord("*"):
            in_block_comment = True
            index += 2
            continue
        if char == ord('"'):
            in_string = True
            index += 1
            continue
        if char == ord("'"):
            in_char = True
            index += 1
            continue
        if char == ord("("):
            depth += 1
        elif char == ord(")"):
            depth -= 1
            if depth == 0:
                return index
        index += 1

    return None


def find_matching_brace(data: bytes, open_index: int) -> int | None:
    depth = 0
    index = open_index
    in_line_comment = False
    in_block_comment = False
    in_string = False
    in_char = False
    escaped = False

    while index < len(data):
        char = data[index]
        next_char = data[index + 1] if index + 1 < len(data) else None

        if in_line_comment:
            if char == ord("\n"):
                in_line_comment = False
            index += 1
            continue
        if in_block_comment:
            if char == ord("*") and next_char == ord("/"):
                in_block_comment = False
                index += 2
            else:
                index += 1
            continue
        if in_string:
            if escaped:
                escaped = False
            elif char == ord("\\"):
                escaped = True
            elif char == ord('"'):
                in_string = False
            index += 1
            continue
        if in_char:
            if escaped:
                escaped = False
            elif char == ord("\\"):
                escaped = True
            elif char == ord("'"):
                in_char = False
            index += 1
            continue

        if char == ord("/") and next_char == ord("/"):
            in_line_comment = True
            index += 2
            continue
        if char == ord("/") and next_char == ord("*"):
            in_block_comment = True
            index += 2
            continue
        if char == ord('"'):
            in_string = True
            index += 1
            continue
        if char == ord("'"):
            in_char = True
            index += 1
            continue
        if char == ord("{"):
            depth += 1
        elif char == ord("}"):
            depth -= 1
            if depth == 0:
                return index
        index += 1

    return None


def extract_if_condition(cursor, cache: dict[Path, bytes]) -> str | None:
    location_file = cursor.location.file
    if location_file is None:
        return None
    path = Path(str(location_file)).resolve()
    data = source_bytes(path, cache)
    start = cursor.extent.start.offset
    end = cursor.extent.end.offset
    if start is None or end is None or start >= end:
        return None

    snippet = data[start:end]
    if_index = snippet.find(b"if")
    if if_index < 0:
        return None
    open_index = snippet.find(b"(", if_index + 2)
    if open_index < 0:
        return None
    close_index = find_matching_paren(snippet, open_index)
    if close_index is None:
        return None
    return snippet[open_index + 1 : close_index].decode("utf-8", errors="replace").strip()


def extract_cursor_text(cursor, cache: dict[Path, bytes]) -> str:
    location_file = cursor.location.file
    if location_file is None:
        return ""
    path = Path(str(location_file)).resolve()
    data = source_bytes(path, cache)
    start = cursor.extent.start.offset
    end = cursor.extent.end.offset
    if start is None or end is None or start >= end:
        return ""
    return data[start:end].decode("utf-8", errors="replace")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//.*", " ", text)
    return text


def normalize_expr(expr: str) -> str:
    expr = strip_comments(expr)
    expr = re.sub(r"\s+", " ", expr).strip()
    expr = strip_outer_parens(expr)
    expr = re.sub(r"\bNULL\b", "nullptr", expr)
    expr = re.sub(r"\band\b", "&&", expr)
    expr = re.sub(r"\bor\b", "||", expr)
    expr = re.sub(r"\s+", " ", expr).strip()
    expr = re.sub(r"\s*([()!,?:~+\-*/%^<>=&|])\s*", r"\1", expr)
    expr = re.sub(r"\s*(->|::|\.)\s*", r"\1", expr)
    return canonical_null_comparison(strip_outer_parens(expr))


def strip_outer_parens(expr: str) -> str:
    expr = expr.strip()
    while expr.startswith("(") and expr.endswith(")") and matching_outer_parens(expr):
        expr = expr[1:-1].strip()
    return expr


def matching_outer_parens(expr: str) -> bool:
    depth = 0
    for index, char in enumerate(expr):
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0 and index != len(expr) - 1:
                return False
        if depth < 0:
            return False
    return depth == 0


def canonical_null_comparison(expr: str) -> str:
    match = re.fullmatch(r"(.+?)(==|!=)(nullptr|0)", expr)
    if match:
        left, op, right = match.groups()
        if right == "0" and left.strip().isdigit():
            return expr
        return f"{left}{op}nullptr"
    match = re.fullmatch(r"(nullptr|0)(==|!=)(.+)", expr)
    if match:
        left, op, right = match.groups()
        if left == "0" and right.strip().isdigit():
            return expr
        return f"{right}{op}nullptr"
    return expr


def split_boolean_atoms(expr: str) -> tuple[str, ...]:
    expr = strip_outer_parens(normalize_expr(expr))
    parts = split_top_level(expr, "||")
    if len(parts) == 1:
        parts = split_top_level(expr, "&&")
    if len(parts) == 1:
        return (normalize_expr(expr),) if expr else ()

    atoms: list[str] = []
    for part in parts:
        atoms.extend(split_boolean_atoms(part))
    return tuple(atom for atom in atoms if atom)


def split_top_level(expr: str, operator: str) -> list[str]:
    parts: list[str] = []
    depth = 0
    index = 0
    start = 0
    in_string = False
    in_char = False
    escaped = False

    while index < len(expr):
        char = expr[index]
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            index += 1
            continue
        if in_char:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == "'":
                in_char = False
            index += 1
            continue
        if char == '"':
            in_string = True
            index += 1
            continue
        if char == "'":
            in_char = True
            index += 1
            continue
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif depth == 0 and expr.startswith(operator, index):
            parts.append(expr[start:index])
            index += len(operator)
            start = index
            continue
        index += 1

    if parts:
        parts.append(expr[start:])
        return [part.strip() for part in parts if part.strip()]
    return [expr.strip()]


def predicate_calls(atom: str) -> tuple[str, ...]:
    calls = []
    for match in re.finditer(r"(?:->|\.|::)?\b([A-Za-z_]\w*)\s*\(", atom):
        name = match.group(1)
        if name not in KEYWORDS:
            calls.append(name)
    return tuple(sorted(set(calls)))


def predicate_call_keys(atom: str) -> tuple[str, ...]:
    keys: list[str] = []
    for call in predicate_calls(atom):
        lower = call.lower()
        if lower == "isavailable" and not is_window_liveness_atom(atom):
            continue
        if lower.startswith(PREDICATE_CALL_PREFIXES) or lower in {"deleted", "liveLvObj".lower()}:
            keys.append(call)
    return tuple(sorted(set(keys)))


def predicate_variable_keys(atom: str) -> tuple[str, ...]:
    keys: list[str] = []
    null_match = re.fullmatch(r"(.+?)(==|!=)nullptr", atom)
    if null_match:
        keys.append(canonical_variable_name(null_match.group(1)))
    negated_match = re.fullmatch(r"!\(?([A-Za-z_]\w*(?:(?:->|\.)[A-Za-z_]\w*)*)\)?", atom)
    if negated_match:
        keys.append(canonical_variable_name(negated_match.group(1)))
    return tuple(sorted(set(key for key in keys if key)))


def canonical_variable_name(expr: str) -> str:
    expr = expr.strip()
    expr = re.sub(r"^(this->)", "", expr)
    return expr


def family_keys(atom: str) -> tuple[str, ...]:
    lower = atom.lower()
    keys: list[str] = []
    if is_invariant_boundary_atom(atom):
        return ()
    if any(part in lower for part in ("lvobj", "lv_obj", "haslvobj", "livelvobj", "getlvobj")):
        keys.append("LVGL object presence/lifetime")
    if is_window_liveness_atom(atom):
        keys.append("Window availability/event liveness")
    if "content" in lower:
        keys.append("Content pointer presence")
    return tuple(sorted(set(keys)))


def is_invariant_boundary_atom(atom: str) -> bool:
    return any(f"{name}(" in atom for name in INVARIANT_BOUNDARY_CALLS)


def is_window_liveness_atom(atom: str) -> bool:
    lower = atom.lower()
    if any(part in lower for part in ("acceptsevents", "deleted", "_deleted")):
        return True
    if "isavailable" not in lower:
        return False

    if re.search(r"(?:^|[!(&|])(?:this->)?isavailable\s*\(", lower):
        return True

    receiver_match = re.search(
        r"([a-z_]\w*)\s*(?:->|\.)\s*isavailable\s*\(",
        lower,
    )
    return bool(receiver_match and receiver_match.group(1) in WINDOW_LIFETIME_RECEIVERS)


def is_interesting_atom(atom: str) -> bool:
    lower = atom.lower()
    if is_invariant_boundary_atom(atom):
        return False
    if atom.startswith("!") or "nullptr" in atom:
        return True
    if "isavailable" in lower and is_window_liveness_atom(atom):
        return True
    if any(part in lower for part in INTERESTING_SUBSTRINGS if part != "isavailable"):
        return True
    return bool(predicate_call_keys(atom))


def ui_safety_variable_name(key: str) -> str:
    key = key.strip()
    key = re.sub(r"^(?:this->)", "", key)
    key = re.sub(r"\(.*\)$", "", key)
    return re.split(r"->|\.", key)[-1].lower()


def is_ui_safety_atom(atom: str) -> bool:
    lower = atom.lower()
    if is_invariant_boundary_atom(atom):
        return False
    if any(part in lower for part in UI_SAFETY_ATOM_SUBSTRINGS):
        return True
    return any(
        ui_safety_variable_name(variable) in UI_SAFETY_VARIABLES
        for variable in predicate_variable_keys(atom)
    )


def is_ui_safety_record(record: IfRecord) -> bool:
    if is_allowed_ui_safety_boundary_record(record):
        return False
    if any(key in UI_SAFETY_FAMILIES for key in record.family_keys):
        if any(is_allowed_family_boundary(record, key) for key in record.family_keys):
            return False
        return True
    if any(call in UI_SAFETY_CALLS for call in record.call_keys):
        return True
    if any(ui_safety_variable_name(key) in UI_SAFETY_VARIABLES for key in record.variable_keys):
        return True
    return any(is_ui_safety_atom(atom) for atom in record.atoms)


def is_allowed_ui_safety_boundary_record(record: IfRecord) -> bool:
    path = record.file.as_posix()
    if not any(path.endswith(suffix) for suffix in WINDOW_LIFETIME_BOUNDARY_SUFFIXES):
        return False
    if any(ui_safety_variable_name(key) in UI_SAFETY_VARIABLES for key in record.variable_keys):
        return True
    return any(is_ui_safety_atom(atom) for atom in record.atoms)


def body_summary(cursor, cache: dict[Path, bytes]) -> tuple[str, tuple[str, ...]]:
    text = extract_cursor_text(cursor, cache)
    compact = one_line(text)
    calls = tuple(sorted(set(body_calls(compact))))

    emit_match = re.search(r"\bemitEvent\s*\(", compact)
    if emit_match:
        return ("call emitEvent(...)", calls)

    return_match = re.search(r"\breturn\b\s*([^;]*);", compact)
    if return_match:
        returned = return_match.group(1).strip()
        if returned:
            return (f"return {returned}", calls)
        return ("return", calls)

    if re.search(r"\b(?:break|continue)\s*;", compact):
        action = re.search(r"\b(?:break|continue)\b", compact)
        if action:
            return (action.group(0), calls)

    if any(call.startswith("lv_") for call in calls):
        return ("LVGL call", calls)

    if calls:
        return (f"call {', '.join(calls[:3])}", calls)

    if re.search(r"(?<![=!<>])=(?!=)", compact):
        return ("assignment/mutation", calls)

    if compact:
        return (compact[:80], calls)
    return ("empty body", calls)


def one_line(text: str) -> str:
    text = strip_comments(text)
    text = re.sub(r"\s+", " ", text).strip()
    if text.startswith("{") and text.endswith("}"):
        text = text[1:-1].strip()
    return text


def body_calls(text: str) -> list[str]:
    calls = []
    for match in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", text):
        name = match.group(1)
        if name not in KEYWORDS:
            calls.append(name)
    return calls


def if_then_cursor(cursor):
    children = list(cursor.get_children())
    if len(children) < 2:
        return None
    return children[1]


def parse_translation_unit(cindex, index, command: CompileCommand, parse_as: Path | None = None):
    source_file = parse_as or command.file
    args = list(command.args)
    if parse_as is not None and is_header_path(parse_as):
        args = ["-x", "c++", *args]
    return index.parse(
        str(source_file),
        args=args,
        options=0,
    )


def collect_if_records(
    selected_files: set[Path],
    commands: dict[Path, CompileCommand],
    tu_commands: list[CompileCommand],
    cindex,
    root: Path,
    verbose: bool,
) -> tuple[list[IfRecord], list[str]]:
    index = cindex.Index.create()
    cache: dict[Path, bytes] = {}
    records_by_location: dict[tuple[Path, int, int, str], IfRecord] = {}
    diagnostics: list[str] = []

    parsed_files: set[Path] = set()

    def parse_and_collect(command: CompileCommand, parse_as: Path | None = None) -> None:
        source = parse_as or command.file
        parsed_files.add(source)
        try:
            tu = parse_translation_unit(cindex, index, command, parse_as)
        except cindex.TranslationUnitLoadError as exc:
            diagnostics.append(f"{relpath(source, root)}: failed to parse: {exc}")
            return

        if verbose:
            for diagnostic in tu.diagnostics:
                diagnostics.append(f"{relpath(source, root)}: {diagnostic.spelling}")

        def visit(cursor) -> None:
            location_file = cursor.location.file
            if location_file is not None:
                cursor_file = Path(str(location_file)).resolve()
                if cursor_file not in selected_files:
                    return
            if cursor.kind == cindex.CursorKind.IF_STMT:
                record = build_if_record(cursor, selected_files, cache, root)
                if record is not None:
                    records_by_location[
                        (record.file, record.line, record.column, record.normalized_condition)
                    ] = record
            for child in cursor.get_children():
                visit(child)

        visit(tu.cursor)

    for command in tu_commands:
        parse_and_collect(command)

    covered_files = {record.file for record in records_by_location.values()}
    missing_headers = {
        path for path in selected_files if is_header_path(path) and path not in covered_files
    }
    for header in sorted(missing_headers, key=lambda p: relpath(p, root)):
        command = nearest_compile_command(header, commands)
        if command is not None and header not in parsed_files:
            parse_and_collect(command, header)

    return list(records_by_location.values()), diagnostics


def build_if_record(cursor, selected_files: set[Path], cache: dict[Path, bytes], root: Path) -> IfRecord | None:
    location_file = cursor.location.file
    if location_file is None:
        return None
    file = Path(str(location_file)).resolve()
    if file not in selected_files:
        return None

    condition = extract_if_condition(cursor, cache)
    if not condition:
        return None

    normalized_condition = normalize_expr(condition)
    atoms = split_boolean_atoms(normalized_condition)
    call_keys = tuple(sorted({key for atom in atoms for key in predicate_call_keys(atom)}))
    variable_keys = tuple(sorted({key for atom in atoms for key in predicate_variable_keys(atom)}))
    families = tuple(sorted({key for atom in atoms for key in family_keys(atom)}))
    then_cursor = if_then_cursor(cursor)
    action, calls = body_summary(then_cursor, cache) if then_cursor is not None else ("unknown body", ())
    function = enclosing_function_name(cursor)
    if "ForTest" not in function:
        function = enclosing_for_test_function_from_source(file, cursor.location.line, cache) or function

    return IfRecord(
        file=file,
        line=cursor.location.line,
        column=cursor.location.column,
        condition=one_line(condition),
        normalized_condition=normalized_condition,
        atoms=atoms,
        call_keys=call_keys,
        variable_keys=variable_keys,
        family_keys=families,
        body_action=action,
        body_calls=calls,
        function=function,
        scope=scope_for(file, root, function),
    )


def enclosing_function_name(cursor) -> str:
    function_kinds = {
        "CXX_METHOD",
        "CONSTRUCTOR",
        "CONVERSION_FUNCTION",
        "DESTRUCTOR",
        "FUNCTION_DECL",
        "FUNCTION_TEMPLATE",
    }
    stack = [cursor.lexical_parent, cursor.semantic_parent]
    seen: set[int] = set()
    while stack:
        parent = stack.pop(0)
        if parent is None:
            continue
        parent_hash = hash(parent)
        if parent_hash in seen:
            continue
        seen.add(parent_hash)
        if getattr(parent.kind, "name", "") in function_kinds:
            spelling = getattr(parent, "spelling", "") or getattr(parent, "displayname", "")
            if spelling:
                return spelling
        stack.append(parent.lexical_parent)
        stack.append(parent.semantic_parent)
    return ""


def enclosing_for_test_function_from_source(path: Path, line: int, cache: dict[Path, bytes]) -> str:
    data = source_bytes(path, cache)
    source = data.decode("utf-8", errors="ignore")
    line_offsets = [0]
    for match in re.finditer(r"\n", source):
        line_offsets.append(match.end())
    if line <= 0 or line > len(line_offsets):
        return ""

    target_offset = line_offsets[line - 1]
    pattern = (
        rb"\b([A-Za-z_][A-Za-z0-9_]*ForTest)\s*\([^;{}]*\)"
        rb"(?:\s*->\s*[A-Za-z_:][A-Za-z0-9_:<>]*)?\s*\{"
    )
    for match in re.finditer(pattern, data, re.MULTILINE):
        open_brace = match.end() - 1
        if open_brace > target_offset:
            break
        close_brace = find_matching_brace(data, open_brace)
        if close_brace is not None and open_brace <= target_offset <= close_brace:
            return match.group(1).decode("utf-8", errors="ignore")
    return ""


def scope_for(path: Path, root: Path, function: str = "") -> str:
    rel = relpath(path, root)
    if "/tests/" in f"/{rel}/" or rel.startswith("radio/src/tests/"):
        return "tests"
    if function.endswith("ForTest") or "ForTest" in function:
        return "tests"
    return "production"


def groups_for_exact(records: Iterable[IfRecord]) -> list[Group]:
    grouped: dict[str, list[IfRecord]] = collections.defaultdict(list)
    for record in records:
        grouped[record.normalized_condition].append(record)
    return [Group(key, tuple(values)) for key, values in grouped.items()]


def groups_for_atoms(records: Iterable[IfRecord]) -> list[Group]:
    grouped: dict[str, list[IfRecord]] = collections.defaultdict(list)
    for record in records:
        for atom in set(record.atoms):
            if is_interesting_atom(atom):
                grouped[atom].append(record)
    return [Group(key, tuple(values)) for key, values in grouped.items()]


def groups_for_calls(records: Iterable[IfRecord]) -> list[Group]:
    grouped: dict[str, list[IfRecord]] = collections.defaultdict(list)
    for record in records:
        for key in record.call_keys:
            grouped[key].append(record)
    return [Group(key, tuple(values)) for key, values in grouped.items()]


def groups_for_variables(records: Iterable[IfRecord]) -> list[Group]:
    grouped: dict[str, list[IfRecord]] = collections.defaultdict(list)
    for record in records:
        for key in record.variable_keys:
            grouped[key].append(record)
    return [Group(key, tuple(values)) for key, values in grouped.items()]


def groups_for_families(records: Iterable[IfRecord]) -> list[Group]:
    grouped: dict[str, list[IfRecord]] = collections.defaultdict(list)
    for record in records:
        for key in record.family_keys:
            if is_allowed_family_boundary(record, key):
                continue
            grouped[key].append(record)
    return [Group(key, tuple(values)) for key, values in grouped.items()]


def is_allowed_family_boundary(record: IfRecord, family: str) -> bool:
    if family not in {
        "LVGL object presence/lifetime",
        "Window availability/event liveness",
    }:
        return False
    path = record.file.as_posix()
    return any(path.endswith(suffix) for suffix in WINDOW_LIFETIME_BOUNDARY_SUFFIXES)


def interesting_groups(
    groups: Iterable[Group],
    min_occurrences: int,
    show_local: bool,
) -> list[Group]:
    filtered: list[Group] = []
    for group in groups:
        if group.count < min_occurrences:
            continue
        if not show_local and len(group.files) < 2:
            continue
        filtered.append(group)
    return sorted(filtered, key=group_sort_key)


def group_sort_key(group: Group) -> tuple[int, int, int, str]:
    action_count = len({record.body_action for record in group.records})
    return (-len(group.files), -group.count, -action_count, group.key)


def format_report(
    records: list[IfRecord],
    selected_files: set[Path],
    diagnostics: list[str],
    root: Path,
    args: argparse.Namespace,
) -> str:
    production_records = [record for record in records if record.scope == "production"]
    test_records = [record for record in records if record.scope == "tests"]

    lines: list[str] = []
    mode = f"paths: {', '.join(args.paths)}" if args.paths else f"changed files: {args.base}...HEAD"
    lines.append("# Repeated if-invariant report")
    lines.append("")
    lines.append(f"- Mode: {mode}")
    lines.append(f"- Compile database: {relpath(Path(args.compile_commands_path), root)}")
    if args.ui_safety:
        lines.append("- Filter: UI safety invariants only")
    lines.append(f"- Files selected: {len(selected_files)}")
    lines.append(f"- If statements analyzed: {len(records)} ({len(production_records)} production, {len(test_records)} tests)")
    if diagnostics and args.verbose:
        lines.append(f"- Parse diagnostics: {len(diagnostics)}")
    elif diagnostics:
        lines.append(f"- Parse diagnostics: {len(diagnostics)} hidden; rerun with --verbose to inspect")
    lines.append("")

    append_scope_report(lines, "Production", production_records, root, args)
    if args.include_tests and test_records:
        lines.append("")
        append_scope_report(lines, "Tests", test_records, root, args)

    if args.verbose and diagnostics:
        lines.append("")
        lines.append("## Parse diagnostics")
        for diagnostic in diagnostics[: args.max_examples]:
            lines.append(f"- {diagnostic}")
        if len(diagnostics) > args.max_examples:
            lines.append(f"- ... {len(diagnostics) - args.max_examples} more")

    return "\n".join(lines)


def append_scope_report(
    lines: list[str],
    title: str,
    records: list[IfRecord],
    root: Path,
    args: argparse.Namespace,
) -> None:
    if args.ui_safety:
        records = [record for record in records if is_ui_safety_record(record)]

    lines.append(f"## {title}")
    if not records:
        lines.append("")
        lines.append("No repeated invariant-looking if conditions found.")
        return

    sections = (
        ("Invariant families", groups_for_families(records), "family"),
        ("Repeated predicate atoms", groups_for_atoms(records), "atom"),
        ("Repeated predicate calls", groups_for_calls(records), "call"),
        ("Repeated null/boolean variables", groups_for_variables(records), "variable"),
        ("Repeated full conditions", groups_for_exact(records), "condition"),
    )

    any_section = False
    hidden_local = 0
    for section_title, raw_groups, label in sections:
        groups = interesting_groups(raw_groups, args.min_occurrences, args.show_local)
        local_only = [
            group
            for group in raw_groups
            if group.count >= args.min_occurrences and len(group.files) < 2
        ]
        if not args.show_local:
            hidden_local += len(local_only)
        if not groups:
            continue
        any_section = True
        lines.append("")
        lines.append(f"### {section_title}")
        for group in groups[: args.max_groups]:
            append_group(lines, label, group, root, args.max_examples)

    if not any_section:
        lines.append("")
        lines.append("No repeated invariant-looking if conditions crossed file boundaries.")
    if hidden_local and not args.show_local:
        lines.append("")
        lines.append(
            f"Hidden local-only repeats: {hidden_local}. "
            "Use --show-local when reviewing duplication inside a single file."
        )


def append_group(lines: list[str], label: str, group: Group, root: Path, max_examples: int) -> None:
    actions = collections.Counter(record.body_action for record in group.records)
    atoms = collections.Counter(atom for record in group.records for atom in record.atoms)
    lines.append("")
    lines.append(
        f"- {label} `{group.key}`: {group.count} ifs across {len(group.files)} files; "
        f"actions: {format_counter(actions, 3)}"
    )
    if label in {"family", "call", "variable"}:
        lines.append(f"  atoms: {format_counter(atoms, 4)}")
    for record in sorted(group.records, key=lambda item: (relpath(item.file, root), item.line))[:max_examples]:
        location = f"{relpath(record.file, root)}:{record.line}"
        lines.append(
            f"  - {location}: if `{record.normalized_condition}` -> {record.body_action}"
        )
    extra = group.count - max_examples
    if extra > 0:
        lines.append(f"  - ... {extra} more")


def format_counter(counter: collections.Counter[str], limit: int) -> str:
    parts = [f"{name} x{count}" for name, count in counter.most_common(limit)]
    if len(counter) > limit:
        parts.append(f"+{len(counter) - limit} more")
    return ", ".join(parts)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Find repeated invariant-like if conditions in changed C/C++ files or paths."
    )
    parser.add_argument(
        "paths",
        nargs="*",
        help="Files or folders to scan. If omitted, scans C/C++ files changed on this branch.",
    )
    parser.add_argument(
        "--base",
        default="main",
        help="Base branch for changed-file mode (default: main).",
    )
    parser.add_argument(
        "--compile-commands",
        help="Path to compile_commands.json (default: build/native, then build/arm-none-eabi).",
    )
    parser.add_argument(
        "--min-occurrences",
        type=int,
        default=2,
        help="Minimum occurrences before a group is reported (default: 2).",
    )
    parser.add_argument(
        "--max-groups",
        type=int,
        default=8,
        help="Maximum groups per section (default: 8).",
    )
    parser.add_argument(
        "--max-examples",
        type=int,
        default=6,
        help="Maximum source examples per group (default: 6).",
    )
    parser.add_argument(
        "--show-local",
        action="store_true",
        help="Also report repeats confined to one file.",
    )
    parser.add_argument(
        "--ui-safety",
        action="store_true",
        help=(
            "Report only repeated UI lifetime, availability, and required-child "
            "guards. Use this for Bob-style UI safety reviews."
        ),
    )
    parser.add_argument(
        "--include-tests",
        action="store_true",
        help="Also print repeats from test files and ForTest helpers.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show parse diagnostics.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    root = repo_root()
    compile_commands_path = find_compile_commands(root, args.compile_commands)
    args.compile_commands_path = str(compile_commands_path)

    selected_files = (
        collect_cpp_files(root, args.paths)
        if args.paths
        else changed_cpp_files(root, args.base)
    )
    if not selected_files:
        print("No C/C++ files selected.")
        return 0

    commands = load_compile_commands(compile_commands_path)
    tu_commands = commands_to_parse(selected_files, commands, args.paths, root)
    if not tu_commands:
        raise SystemExit("No selected source files were present in compile_commands.json.")

    cindex = import_clang()
    records, diagnostics = collect_if_records(
        selected_files,
        commands,
        tu_commands,
        cindex,
        root,
        args.verbose,
    )
    print(format_report(records, selected_files, diagnostics, root, args))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
