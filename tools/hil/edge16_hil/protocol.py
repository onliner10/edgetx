from __future__ import annotations

from dataclasses import dataclass
import shlex


class ProtocolError(ValueError):
    pass


@dataclass(frozen=True)
class UnoEvent:
    kind: str
    fields: dict[str, str]
    raw: str


def parse_event_line(line: str) -> UnoEvent:
    raw = line.strip()
    if not raw:
        raise ProtocolError("empty line")

    try:
        parts = shlex.split(raw, posix=True)
    except ValueError as exc:
        raise ProtocolError(f"malformed quoted field: {exc}") from exc

    if not parts:
        raise ProtocolError("empty line")

    kind = parts[0].upper()
    fields: dict[str, str] = {}
    for token in parts[1:]:
        if "=" not in token:
            raise ProtocolError(f"field without '=': {token!r}")
        key, value = token.split("=", 1)
        if not key:
            raise ProtocolError(f"empty key in field: {token!r}")
        fields[key] = value

    return UnoEvent(kind=kind, fields=fields, raw=raw)


def format_command(name: str, **fields: object) -> str:
    parts = [name.upper()]
    for key, value in fields.items():
        if value is None:
            continue
        key_s = str(key)
        value_s = str(value)
        if not key_s or any(ch.isspace() or ch == "=" for ch in key_s):
            raise ProtocolError(f"invalid command key: {key_s!r}")
        if any(ch.isspace() for ch in value_s):
            value_s = shlex.quote(value_s)
        parts.append(f"{key_s}={value_s}")
    return " ".join(parts)


def field_int(event: UnoEvent, name: str, default: int | None = None) -> int:
    value = event.fields.get(name)
    if value is None:
        if default is None:
            raise ProtocolError(f"{event.kind} missing integer field {name!r}")
        return default
    try:
        return int(value, 0)
    except ValueError as exc:
        raise ProtocolError(f"{event.kind} field {name!r} is not an integer: {value!r}") from exc
