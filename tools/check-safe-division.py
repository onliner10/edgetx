#!/usr/bin/env python3

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "radio/src"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
EXCLUDED_PARTS = {"tests", "thirdparty", "translations"}
ALLOW_MARKER = "edge16-safe-divide: allow"

CRITICAL_FILES = {
    "radio/src/analogs.cpp",
    "radio/src/curves.cpp",
    "radio/src/functions.cpp",
    "radio/src/input_mapping.cpp",
    "radio/src/mixer.cpp",
    "radio/src/mixer_scheduler.cpp",
    "radio/src/mixes.cpp",
    "radio/src/sbus.cpp",
    "radio/src/switches.cpp",
    "radio/src/timers.cpp",
    "radio/src/trainer.cpp",
}
CRITICAL_PREFIXES = (
    "radio/src/pulses/",
    "radio/src/tasks/",
)

IDENTIFIER_RE = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
NUMBER_RE = re.compile(
    r"""
    (?:
      0[xX][0-9A-Fa-f]+
      |
      (?:
        (?:\d+(?:\.\d*)?|\.\d+)
        (?:[eE][+-]?\d+)?
      )
    )
    [uUlLfF]*
    """,
    re.VERBOSE,
)


def iter_source_files():
    for path in sorted(SOURCE_ROOT.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        relative_to_source = path.relative_to(SOURCE_ROOT)
        if any(part in EXCLUDED_PARTS for part in relative_to_source.parts):
            continue
        relative = path.relative_to(ROOT).as_posix()
        if relative not in CRITICAL_FILES and not relative.startswith(CRITICAL_PREFIXES):
            continue
        yield path


def strip_comments_and_strings(line, in_block_comment):
    result = []
    i = 0
    in_string = False
    quote = ""

    while i < len(line):
        c = line[i]
        n = line[i + 1] if i + 1 < len(line) else ""

        if in_block_comment:
            if c == "*" and n == "/":
                result.extend("  ")
                i += 2
                in_block_comment = False
            else:
                result.append(" ")
                i += 1
            continue

        if in_string:
            if c == "\\":
                result.append(" ")
                if i + 1 < len(line):
                    result.append(" ")
                i += 2
                continue
            if c == quote:
                in_string = False
                quote = ""
            result.append(" ")
            i += 1
            continue

        if c == "/" and n == "/":
            result.extend(" " * (len(line) - i))
            break
        if c == "/" and n == "*":
            result.extend("  ")
            i += 2
            in_block_comment = True
            continue
        if c == '"' or c == "'":
            in_string = True
            quote = c
            result.append(" ")
            i += 1
            continue

        result.append(c)
        i += 1

    return "".join(result), in_block_comment


def numeric_value(token):
    token = token.rstrip("uUlLfF")
    try:
        return float.fromhex(token) if token.lower().startswith("0x") else float(token)
    except ValueError:
        return None


def constant_expression(expr):
    expr = expr.strip()
    if not expr:
        return False

    if expr.startswith("sizeof"):
        return True

    number_match = NUMBER_RE.fullmatch(expr)
    if number_match:
        value = numeric_value(expr)
        return value is not None and value != 0

    identifiers = IDENTIFIER_RE.findall(expr)
    if not identifiers:
        return True

    for identifier in identifiers:
        if identifier in {"true", "false", "sizeof"}:
            continue
        if identifier != identifier.upper():
            return False

    return True


def expression_after_operator(code, start):
    i = start
    while i < len(code) and code[i].isspace():
        i += 1

    if i < len(code) and code[i] == "=":
        i += 1
        while i < len(code) and code[i].isspace():
            i += 1

    if i >= len(code):
        return ""

    if code[i] == "(":
        depth = 0
        for j in range(i, len(code)):
            if code[j] == "(":
                depth += 1
            elif code[j] == ")":
                depth -= 1
                if depth == 0:
                    return code[i + 1 : j]
        return code[i + 1 :]

    if code.startswith("sizeof", i):
        return "sizeof"

    number_match = NUMBER_RE.match(code, i)
    if number_match:
        return number_match.group(0)

    identifier_match = IDENTIFIER_RE.match(code, i)
    if identifier_match:
        return identifier_match.group(0)

    return code[i : i + 1]


def violation_columns(code):
    columns = []
    for i, c in enumerate(code):
        if c not in "/%":
            continue

        prev = code[i - 1] if i > 0 else ""
        nxt = code[i + 1] if i + 1 < len(code) else ""
        if c == "/" and (prev == "/" or nxt in "/*"):
            continue

        denominator = expression_after_operator(code, i + 1)
        if not constant_expression(denominator):
            columns.append(i + 1)

    return columns


def main():
    errors = []

    for path in iter_source_files():
        in_block_comment = False
        relative = path.relative_to(ROOT)
        for line_no, line in enumerate(path.read_text(errors="ignore").splitlines(), start=1):
            code, in_block_comment = strip_comments_and_strings(line, in_block_comment)
            if ALLOW_MARKER in line:
                continue

            for column in violation_columns(code):
                errors.append(
                    f"{relative}:{line_no}:{column}: raw dynamic division/modulo; "
                    "use divOr, divInto, or modOr"
                )

    if errors:
        print("Safe division policy violations:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
