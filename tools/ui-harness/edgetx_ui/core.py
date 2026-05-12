import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import atexit
from collections import deque
import select
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .png import convert_ppm_to_png


def _resolve_repo_root() -> Path:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            capture_output=True, text=True, check=False,
        )
        if result.returncode == 0 and result.stdout.strip():
            return Path(result.stdout.strip())
    except Exception:
        pass
    return Path(__file__).resolve().parents[3]


REPO_ROOT = _resolve_repo_root()

_ORPHANED_SIMU_LOCKFILES: list[Path] = []


def _cleanup_stale_lockfile(lockfile: Path) -> bool:
    """Clean up a single stale lockfile. Returns True if cleaned, False otherwise."""
    try:
        if not lockfile.exists():
            return False
        pid = int(lockfile.read_text().strip())
        try:
            os.kill(pid, 0)
            return False
        except OSError:
            lockfile.unlink()
            return True
    except Exception:
        try:
            lockfile.unlink()
            return True
        except Exception:
            return False


def _cleanup_orphaned_simulators() -> None:
    for lockfile in _ORPHANED_SIMU_LOCKFILES:
        _cleanup_stale_lockfile(lockfile)
    _ORPHANED_SIMU_LOCKFILES.clear()


def _discover_and_clean_stale_locks() -> None:
    """Called at module load to clean any lockfiles left behind by dead processes."""
    tmpdir = Path(tempfile.gettempdir())
    for lockfile in tmpdir.glob("edgetx-simulator-*.lock"):
        _cleanup_stale_lockfile(lockfile)


_discover_and_clean_stale_locks()

atexit.register(_cleanup_orphaned_simulators)


@dataclass(frozen=True)
class Target:
    name: str
    pcb: str
    pcbrev: str | None
    width: int
    height: int


TARGETS = {
    "tx16s": Target("tx16s", "X10", "TX16S", 480, 272),
    "tx16smk3": Target("tx16smk3", "TX16SMK3", None, 800, 480),
}


KEYS = {
    "MENU",
    "EXIT",
    "ENTER",
    "PAGEUP",
    "PAGEDN",
    "UP",
    "DOWN",
    "LEFT",
    "RIGHT",
    "PLUS",
    "MINUS",
    "MODEL",
    "TELE",
    "SYS",
    "SHIFT",
    "BIND",
}


class HarnessError(RuntimeError):
    pass


def repo_root() -> Path:
    return REPO_ROOT


def target_config(target: str) -> Target:
    try:
        return TARGETS[target.lower()]
    except KeyError as exc:
        raise HarnessError(f"unsupported target: {target}") from exc


def build_dir(target: str) -> Path:
    root = os.environ.get("EDGETX_UI_BUILD_ROOT")
    if root:
        return Path(root) / target.lower()
    return REPO_ROOT / "build" / "ui-harness" / target.lower()


def simulator_environment() -> dict[str, str]:
    env = os.environ.copy()
    has_display = env.get("DISPLAY") or env.get("WAYLAND_DISPLAY")
    if sys.platform.startswith("linux") and not has_display:
        env.setdefault("SDL_VIDEODRIVER", "dummy")
    if env.get("TSAN_OPTIONS"):
        # Avoid libdbus lock-order reports from SDL desktop integration while
        # keeping TSan focused on simulator/firmware code.
        env["DBUS_SESSION_BUS_ADDRESS"] = "/dev/null"
    return env


def configure_command(target: str) -> list[str]:
    cfg = target_config(target)
    python_exec = project_python_executable()
    command = [
        "cmake",
        "-S",
        str(REPO_ROOT),
        "-B",
        str(build_dir(cfg.name)),
        "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/native.cmake",
        "-DEdgeTX_SUPERBUILD=OFF",
        "-DNATIVE_BUILD=ON",
        "-DDISABLE_COMPANION=ON",
        "-DSIMU_EXECUTABLE=ON",
        "-DEDGE_TX_BUILD_TESTS=OFF",
        f"-DPython3_EXECUTABLE={python_exec}",
        f"-DPCB={cfg.pcb}",
    ]
    if cfg.pcbrev:
        command.append(f"-DPCBREV={cfg.pcbrev}")
    return command


def build_command(target: str) -> list[str]:
    return ["cmake", "--build", str(build_dir(target)), "--target", "simu"]


def build_simulator(target: str) -> dict[str, Any]:
    cfg = target_config(target)
    commands = [configure_command(cfg.name), build_command(cfg.name)]
    for command in commands:
        result = subprocess.run(
            command,
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if result.returncode != 0:
            raise HarnessError(
                "simulator build failed while running:\n"
                + " ".join(command)
                + "\n\n"
                + result.stdout[-4000:]
            )
    return {"target": cfg.name, "build_dir": str(build_dir(cfg.name)), "executable": str(find_simu_executable(cfg.name))}


def find_simu_executable(target: str) -> Path:
    base = build_dir(target)
    names = ["simu", "simu.exe"]
    for name in names:
        candidate = base / name
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    for name in names:
        for candidate in base.rglob(name):
            if candidate.is_file() and os.access(candidate, os.X_OK):
                return candidate
    raise HarnessError(
        f"could not find built simulator executable under {base}; run `tools/ui-harness/edgetx-ui build {target}`"
    )


def git_commit() -> str:
    result = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def project_python_executable() -> str:
    venv_python = REPO_ROOT / ".venv" / "bin" / "python"
    try:
        if venv_python.exists():
            return str(venv_python)
    except OSError:
        pass
    return sys.executable


class SdlAutomationSession:
    def __init__(
        self,
        target: str = "tx16s",
        sdcard: Path | None = None,
        settings: Path | None = None,
        width: int = 800,
        height: int = 600,
    ) -> None:
        self.target = target_config(target)
        self.runtime_dir: tempfile.TemporaryDirectory[str] | None = None
        self._using_explicit_sdcard = bool(sdcard)
        self.sdcard = Path(sdcard) if self._using_explicit_sdcard else self.runtime_fixture_path("sdcard")
        if settings:
            self.settings = Path(settings)
        elif self._using_explicit_sdcard:
            self.settings = self.runtime_fixture_path("settings", use_fixture=False)
        else:
            self.settings = self.runtime_fixture_path("settings")
        self.width = width
        self.height = height
        self.process: subprocess.Popen[str] | None = None
        self._log_lines: deque[str] = deque(maxlen=2000)

    def runtime_fixture_path(self, kind: str, use_fixture: bool = True) -> Path:
        if self.runtime_dir is None:
            self.runtime_dir = tempfile.TemporaryDirectory(prefix=f"edgetx-ui-{self.target.name}-")

        source = default_fixture_path(kind, self.target.name)
        destination = Path(self.runtime_dir.name) / f"{kind}-{self.target.name}"
        if use_fixture and source.exists():
            shutil.copytree(source, destination, dirs_exist_ok=True)
            if kind == "settings":
                normalize_yaml_line_endings(destination)
        else:
            destination.mkdir(parents=True, exist_ok=True)
        return destination

    def start(self) -> dict[str, Any]:
        if self.process and self.process.poll() is None:
            return self.status()

        if self.runtime_dir is None:
            self.runtime_dir = tempfile.TemporaryDirectory(prefix=f"edgetx-ui-{self.target.name}-")

        if self._using_explicit_sdcard:
            sdcard_copy = Path(self.runtime_dir.name) / f"sdcard-{self.target.name}"
            shutil.copytree(self.sdcard, sdcard_copy, dirs_exist_ok=True)
            self.sdcard = sdcard_copy

        self.sdcard.mkdir(parents=True, exist_ok=True)
        self.settings.mkdir(parents=True, exist_ok=True)
        executable = find_simu_executable(self.target.name)
        self.process = subprocess.Popen(
            [
                str(executable),
                "--width",
                str(self.width),
                "--height",
                str(self.height),
                "--storage",
                str(self.sdcard),
                "--settings",
                str(self.settings),
                "--automation-stdio",
            ],
            cwd=REPO_ROOT,
            env=simulator_environment(),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        lockfile = Path(tempfile.gettempdir()) / f"edgetx-simulator-{self.target.name}-{self.process.pid}.lock"
        lockfile.write_text(str(self.process.pid))
        _ORPHANED_SIMU_LOCKFILES.append(lockfile)

        deadline = time.monotonic() + 5.0
        last_error: Exception | None = None
        last_status: dict[str, Any] | None = None
        poll_interval = 0.1
        while time.monotonic() < deadline:
            try:
                st = self.status()
                last_status = st
                if st.get("startup_completed"):
                    return st
                poll_interval = min(poll_interval * 1.1, 2.0)
                time.sleep(poll_interval)
            except HarnessError as exc:
                last_error = exc
                if self.process.poll() is not None:
                    break
                poll_interval = min(poll_interval * 1.1, 2.0)
                time.sleep(poll_interval)
        if last_status is not None:
            return last_status | {"startup_pending": True}
        if last_error is not None:
            reason = str(last_error)
        elif self.process and self.process.poll() is not None:
            reason = f"process exited with code {self.process.returncode}"
        else:
            reason = "status command did not respond before startup timeout"
        raise HarnessError(f"simulator did not become ready: {reason}")

    def stop(self) -> dict[str, Any]:
        if not self.process:
            return {"running": False}
        if self.process.poll() is None:
            try:
                self.command("stop", timeout=2.0)
            except HarnessError:
                self.process.terminate()
            try:
                self.process.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                self.process.kill()
        if self.runtime_dir is not None:
            self.runtime_dir.cleanup()
            self.runtime_dir = None
        if self._using_explicit_sdcard and self.sdcard.exists():
            shutil.rmtree(self.sdcard, ignore_errors=True)
        for lockfile in Path(tempfile.gettempdir()).glob(f"edgetx-simulator-{self.target.name}-*.lock"):
            try:
                pid = int(lockfile.read_text().strip())
                if pid == self.process.pid if self.process else False:
                    lockfile.unlink()
                    if lockfile in _ORPHANED_SIMU_LOCKFILES:
                        _ORPHANED_SIMU_LOCKFILES.remove(lockfile)
            except Exception:
                pass
        return {"running": False}

    def command(self, command: str, timeout: float = 5.0) -> dict[str, Any]:
        if not self.process or self.process.poll() is not None:
            raise HarnessError("simulator is not running")
        assert self.process.stdin is not None
        assert self.process.stdout is not None
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

        deadline = time.monotonic() + timeout
        recent_lines: deque[str] = deque(maxlen=200)
        buffer = ""
        stdout_fd = self.process.stdout.fileno()
        while time.monotonic() < deadline:
            remaining = max(0.0, deadline - time.monotonic())
            ready, _, _ = select.select([stdout_fd], [], [], remaining)
            if not ready:
                break
            chunk = os.read(stdout_fd, 4096)
            if not chunk:
                break
            buffer += chunk.decode("utf-8", errors="replace")
            response = parse_json_response_line(buffer)
            if response is not None:
                if not response.get("ok", False):
                    raise HarnessError(response.get("error", "simulator command failed"))
                return response
            if '{"ok"' in buffer:
                continue
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.strip()
                if not line:
                    continue
                recent_lines.append(line)
                self._log_lines.append(line)
        tail = "\n".join(recent_lines)
        if tail:
            raise HarnessError(
                f"timed out waiting for simulator response to `{command}`\nRecent output:\n{tail}"
            )
        raise HarnessError(f"timed out waiting for simulator response to `{command}`")

    def status(self) -> dict[str, Any]:
        response = self.command("status")
        result = {
            "target": self.target.name,
            "backend": "sdl-automation",
            "running": bool(response.get("running")),
            "startup_completed": bool(response.get("startup_completed", False)),
            "width": int(response.get("width", 0)),
            "height": int(response.get("height", 0)),
            "depth": int(response.get("depth", 0)),
            "sdcard": str(self.sdcard),
            "settings": str(self.settings),
        }
        if result["running"] and not result["startup_completed"]:
            try:
                blocker = _blocking_dialog_context(self.ui_tree().get("nodes", []))
                if blocker:
                    result["startup_blocker"] = blocker
            except HarnessError:
                pass
        return result

    def get_logs(self, filter_text: str | None = None, tail: int = 200) -> list[str]:
        lines = list(self._log_lines)
        if filter_text:
            lines = [l for l in lines if filter_text in l]
        return lines[-tail:]

    def press(self, key: str, duration_ms: int = 120) -> dict[str, Any]:
        normalized = normalize_key(key)
        return self.command(f"press {normalized} {duration_ms}")

    def long_press(self, key: str, duration_ms: int = 800) -> dict[str, Any]:
        normalized = normalize_key(key)
        return self.command(f"long_press {normalized} {duration_ms}")

    def rotate(self, steps: int) -> dict[str, Any]:
        return self.command(f"rotate {int(steps)}")

    def touch(self, x: int, y: int, duration_ms: int = 120) -> dict[str, Any]:
        return self.command(f"touch {int(x)} {int(y)} {int(duration_ms)}")

    def drag(
        self,
        x1: int,
        y1: int,
        x2: int,
        y2: int,
        duration_ms: int = 300,
        steps: int = 12,
    ) -> dict[str, Any]:
        return self.command(
            f"drag {int(x1)} {int(y1)} {int(x2)} {int(y2)} {int(duration_ms)} {int(steps)}"
        )

    def scroll(
        self,
        direction: str,
        target: dict[str, Any] | str | None = None,
        amount: str = "page",
        duration_ms: int = 300,
    ) -> dict[str, Any]:
        normalized_direction = direction.lower()
        if normalized_direction not in ("up", "down", "left", "right"):
            raise HarnessError("scroll direction must be up, down, left, or right")
        if amount not in ("small", "page"):
            raise HarnessError("scroll amount must be small or page")

        before = self.ui_tree(mode="full", verbose=True)
        mode = "target_drag" if target else "viewport_drag"
        node = self.find_node(target) if target else None
        x1, y1, x2, y2 = (
            _scroll_drag_points(node, normalized_direction, amount)
            if node
            else _viewport_scroll_drag_points(before, normalized_direction, amount)
        )
        response = self.drag(x1, y1, x2, y2, duration_ms=duration_ms, steps=12)
        self.wait(150)
        after = self.ui_tree(mode="full", verbose=True)
        after_node = next((n for n in after.get("nodes", []) if n.get("id") == node.get("id")), None) if node else None
        before_hash = _screen_digest(before)
        after_hash = _screen_digest(after)
        changed = before_hash != after_hash
        warning = ""
        if not changed:
            warning = "Scroll gesture did not change the screen; activate the focused item first or use a different user-equivalent input."
        return {
            "ok": response.get("ok", True),
            "mode": mode,
            "direction": normalized_direction,
            "amount": amount,
            "gesture": [x1, y1, x2, y2],
            "target": _compact_node(node) if node else None,
            "scroll_before": node.get("scroll", {}) if node else {},
            "scroll_after": after_node.get("scroll", {}) if after_node else {},
            "screen_hash_before": before_hash,
            "screen_hash_after": after_hash,
            "changed": changed,
            **({"warning": warning} if warning else {}),
        }

    def wait(self, ms: int) -> dict[str, Any]:
        wait_ms = int(ms)
        return self.command(f"wait {wait_ms}", timeout=max(5.0, wait_ms / 1000.0 + 2.0))

    def set_telemetry(self, name: str, value: float) -> dict[str, Any]:
        return self.command(f"set_telemetry {name} {int(value)}")

    def set_telemetry_streaming(self, enabled: bool) -> dict[str, Any]:
        return self.command(f"telemetry_streaming {1 if enabled else 0}")

    def set_switch(self, index: int, position: int) -> dict[str, Any]:
        return self.command(f"set_switch {int(index)} {int(position)}")

    def switch_sequence(self, steps: list[dict[str, int]]) -> dict[str, Any]:
        encoded_steps = []
        for step in steps:
            encoded_steps.extend(
                [
                    str(int(step["index"])),
                    str(int(step["position"])),
                    str(int(step.get("duration_ms", 0))),
                ]
            )
        return self.command(
            f"switch_sequence {len(steps)} {' '.join(encoded_steps)}"
        )

    def audio_history(self, max_lines: int = 200) -> dict[str, Any]:
        return self.command(f"audio_history {int(max_lines)}")

    def ui_tree(
        self,
        mode: str = "full",
        actionable_only: bool = False,
        text_contains: str | None = None,
        limit: int = 0,
        verbose: bool = True,
    ) -> dict[str, Any]:
        response = self.command("ui_tree", timeout=5.0)
        raw = response.get("ui", {"nodes": []})
        raw = {**raw, "nodes": [_normalized_node(n) for n in raw.get("nodes", [])]}

        if mode not in ("summary", "full"):
            raise HarnessError(f"unknown ui_tree mode: {mode}")

        if mode == "summary":
            summary_nodes = [
                node
                for node in raw.get("nodes", [])
                if node.get("actions") or node.get("text") or node.get("automation_id")
            ]
            filtered_nodes = _filter_nodes(summary_nodes, actionable_only, text_contains, limit)
            result = {
                "screen": raw.get("screen", ""),
                "top_window": raw.get("top_window", ""),
                "focused": raw.get("focused", ""),
                "nodes": filtered_nodes if verbose else [_compact_node(n) for n in filtered_nodes],
                "counts": {"total": len(raw.get("nodes", [])), "returned": len(filtered_nodes)},
                "screen_hash": _screen_digest(raw),
            }
            return result

        filtered_nodes = _filter_nodes(raw.get("nodes", []), actionable_only, text_contains, limit)
        if actionable_only or text_contains or limit > 0:
            raw = {**raw, "nodes": filtered_nodes}

        if not verbose:
            nodes = [_compact_node(n) for n in raw.get("nodes", [])]
            return {**raw, "nodes": nodes}
        return raw

    def screen(self) -> dict[str, Any]:
        tree = self.ui_tree(mode="full", verbose=True)
        nodes = tree.get("nodes", [])
        visible_text = [
            str(n.get("label") or n.get("text", ""))
            for n in nodes
            if n.get("label") or n.get("text")
        ]
        unique_visible_text = list(dict.fromkeys(t for t in visible_text if t))[:30]
        all_actions = [
            _compact_node(n)
            for n in nodes
            if n.get("actions")
        ]
        meaningful_actions = [
            action
            for action in all_actions
            if action.get("label") or action.get("automation_id")
        ]
        actions = (meaningful_actions or all_actions)[:30]
        top_window = tree.get("top_window", "")
        page = _current_page(tree)
        focused = _compact_node(n) if (n := _focused_node(tree)) else ""
        header_title = _header_title(nodes)
        top_window_title = str(page.get("label", "")) if page else ""
        actionable_title = next(
            (
                str(n.get("label") or n.get("text") or n.get("automation_id") or "")
                for n in nodes
                if n.get("actions") and (n.get("label") or n.get("text") or n.get("automation_id"))
            ),
            "",
        )
        non_numeric_title = next(
            (text for text in unique_visible_text if not text.strip().isdigit()),
            unique_visible_text[0] if unique_visible_text else "",
        )
        menu_title = next(
            (text for text in unique_visible_text if "menu" in text.lower()),
            "",
        )
        role_title = next((str(n.get("label") or n.get("text", "")) for n in nodes if n.get("role") == "title"), "")
        title = top_window_title or header_title or role_title or menu_title or actionable_title or non_numeric_title
        scrollables = [_compact_node(n) for n in _screen_scrollables(nodes)[:10]]
        context = _screen_context(title, page, focused, actions, scrollables, nodes)
        return {
            "title": title,
            "page": page or ({"label": title, "kind": "page", "actions": []} if title else {}),
            "context": context,
            "top_window": top_window,
            "focused": focused,
            "screen_hash": _screen_digest(tree),
            "actions": actions,
            "scrollables": scrollables,
            "available_inputs": _available_inputs(nodes),
            "next_actions": _next_actions(context, focused, scrollables),
            "visible_text": unique_visible_text,
            "counts": {
                "total": len(nodes),
                "actions": len(actions),
                "total_actions": len(all_actions),
                "visible_text": len(unique_visible_text),
            },
        }

    def wait_for(
        self,
        text_contains: str | None = None,
        automation_id: str | None = None,
        role: str | None = None,
        timeout_ms: int = 2000,
        poll_ms: int = 200,
    ) -> dict[str, Any]:
        if not text_contains and not automation_id and not role:
            raise HarnessError("wait_for requires text_contains, automation_id, or role")
        deadline = time.monotonic() + timeout_ms / 1000.0
        first_hash: str | None = None
        last_hash = ""
        while time.monotonic() < deadline:
            tree = self.ui_tree(mode="summary", actionable_only=False, verbose=True)
            screen_hash = tree.get("screen_hash", "")
            if first_hash is None:
                first_hash = screen_hash
            for node in tree.get("nodes", []):
                if text_contains:
                    if text_contains.lower() not in _node_search_text(node).lower():
                        continue
                if automation_id and str(node.get("automation_id", "")) != str(automation_id):
                    continue
                if role and str(node.get("role", "")) != str(role):
                    continue
                return {
                    "found": True,
                    "node": _compact_node(node),
                    "screen_hash": screen_hash,
                    "changed": first_hash is not None and screen_hash != first_hash,
                }
            last_hash = screen_hash
            remaining_ms = int(max(0.0, deadline - time.monotonic()) * 1000)
            if remaining_ms <= 0:
                break
            self.wait(min(max(1, poll_ms), remaining_ms))
        return {
            "found": False,
            "screen_hash": last_hash,
            "changed": False,
            "timeout_ms": timeout_ms,
        }

    def find_node(
        self,
        selector: dict[str, Any] | str,
        action: str | None = None,
        near_miss_count: int = 5,
    ) -> dict[str, Any]:
        normalized = normalize_selector(selector)
        tree = self.ui_tree()
        nodes = [node for node in tree.get("nodes", []) if node_matches(node, normalized)]
        required_action = action or normalized.get("action")
        if required_action:
            nodes = [node for node in nodes if required_action in node.get("actions", [])]

        if "index" in normalized:
            index = int(normalized["index"])
            if index < len(nodes):
                return nodes[index]
        elif nodes:
            return nodes[-1] if required_action else nodes[0]

        want_text = _clean_text(normalized.get("text", ""))
        want_text_contains = _clean_text(normalized.get("text_contains", ""))
        want_id = str(normalized.get("automation_id", ""))
        candidates: list[tuple[int, str]] = []
        for node in tree.get("nodes", []):
            score = 0
            n_text = str(node.get("label") or node.get("text", ""))
            n_id = str(node.get("automation_id", ""))
            if want_text and n_text:
                if want_text.lower() in n_text.lower() or n_text.lower() in want_text.lower():
                    score += 10
                elif len(want_text) >= 3 and want_text[:3].lower() == n_text[:3].lower():
                    score += 3
            if want_text_contains and want_text_contains.lower() in _node_search_text(node).lower():
                score += 10
            if want_id and n_id:
                if want_id in n_id or n_id in want_id:
                    score += 10
            if score > 0:
                candidates.append((score, summarize_ui_nodes([node], limit=1)))
        candidates.sort(key=lambda x: -x[0])
        near_miss_lines = "\n".join(f"  [{s}] {line}" for s, line in candidates[:near_miss_count])

        raise HarnessError(
            f"could not find UI node matching {json.dumps(normalized, sort_keys=True)}"
            + (f" with action `{required_action}`" if required_action else "")
            + (f"\n\nNearest visible candidates:\n{near_miss_lines}" if near_miss_lines else "")
            + "\n\nAll visible nodes:\n"
            + summarize_ui_nodes(tree.get("nodes", []))
        )

    def ui_click(self, selector: dict[str, Any] | str, duration_ms: int = 0, verbose: bool = False) -> dict[str, Any]:
        node = self.find_node(selector, "click")
        response = self.command(f"ui_click {node['id']} {int(duration_ms)}", timeout=5.0)
        if verbose:
            return response | {"matched": node, **_action_warning(node)}
        return {
            "ok": response.get("ok", True),
            "matched": _compact_node(node),
            "runtime_id": node.get("id", ""),
            **_action_warning(node),
        }

    def ui_long_click(self, selector: dict[str, Any] | str, duration_ms: int = 0, verbose: bool = False) -> dict[str, Any]:
        node = self.find_node(selector, "long_click")
        response = self.command(f"ui_long_click {node['id']} {int(duration_ms)}", timeout=5.0)
        if verbose:
            return response | {"matched": node}
        return {"ok": response.get("ok", True), "matched": _compact_node(node), "runtime_id": node.get("id", "")}

    def activate(
        self,
        selector: dict[str, Any] | str,
        action: str = "click",
        duration_ms: int = 0,
        verbose: bool = False,
    ) -> dict[str, Any]:
        if action not in ("click", "long_click"):
            raise HarnessError("activate action must be `click` or `long_click`")
        node = self.find_node(selector, action)
        command = "ui_long_click" if action == "long_click" else "ui_click"
        response = self.command(f"{command} {node['id']} {int(duration_ms)}", timeout=5.0)
        if verbose:
            return response | {"matched": node, "action": action, **_action_warning(node)}
        return {
            "ok": response.get("ok", True),
            "action": action,
            "matched": _compact_node(node),
            "runtime_id": node.get("id", ""),
            **_action_warning(node),
        }

    def assert_visible(self, selector: dict[str, Any] | str) -> dict[str, Any]:
        return {"matched": self.find_node(selector)}

    def skip_storage_warning_if_present(self) -> dict[str, Any]:
        results = []
        for _ in range(5):
            tree = self.ui_tree()
            blocker = _blocking_dialog_context(tree.get("nodes", []))
            if not _is_storage_warning_blocker(blocker):
                return {
                    "ok": True,
                    "skipped": bool(results),
                    "count": len(results),
                    "methods": [result["method"] for result in results],
                    "last_result": results[-1].get("result") if results else None,
                }

            self.press("ENTER")
            self.wait(500)
            results.append({"method": "ENTER"})

            tree = self.ui_tree()
            blocker = _blocking_dialog_context(tree.get("nodes", []))
            if not _is_storage_warning_blocker(blocker):
                return {
                    "ok": True,
                    "skipped": True,
                    "count": len(results),
                    "methods": [result["method"] for result in results],
                    "last_result": results[-1].get("result") if results else None,
                }

            try:
                results.append({"method": "dialog.action", "result": self.ui_click({"automation_id": "dialog.action"})})
            except HarnessError:
                results.append({"method": "dialog.action", "error": "not available"})
            self.wait(500)

        tree = self.ui_tree()
        blocker = _blocking_dialog_context(tree.get("nodes", []))
        return {
            "ok": False,
            "skipped": bool(results),
            "count": len(results),
            "methods": [result["method"] for result in results],
            "last_result": results[-1].get("result") if results else None,
            "final_blocker": blocker,
            "warning": "storage warning still visible after bounded skip attempts",
        }

    def screenshot(self, name: str, out_dir: Path) -> dict[str, Any]:
        out_dir.mkdir(parents=True, exist_ok=True)
        safe_name = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in name)
        png_path = out_dir / f"{safe_name}.png"
        metadata_path = out_dir / f"{safe_name}.json"
        with tempfile.NamedTemporaryFile(suffix=".ppm", delete=False) as temp:
            ppm_path = Path(temp.name)
        try:
            response = self.command(f"screenshot_ppm {ppm_path}", timeout=5.0)
            width, height = convert_ppm_to_png(ppm_path, png_path)
        finally:
            ppm_path.unlink(missing_ok=True)

        metadata = {
            "name": name,
            "target": self.target.name,
            "backend": "sdl-automation",
            "width": width,
            "height": height,
            "depth": int(response.get("depth", 0)),
            "path": str(png_path),
            "git_commit": git_commit(),
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }
        metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
        return metadata | {"metadata": str(metadata_path)}


def normalize_key(key: str) -> str:
    normalized = key.upper()
    if normalized not in KEYS:
        raise HarnessError(f"unknown key `{key}`; expected one of {', '.join(sorted(KEYS))}")
    return normalized


def parse_json_response_line(line: str) -> dict[str, Any] | None:
    start = line.find('{"ok"')
    if start < 0:
        start = line.find("{")
    if start < 0:
        return None

    try:
        response, _ = json.JSONDecoder(strict=False).raw_decode(line[start:])
    except json.JSONDecodeError:
        return None

    return response if isinstance(response, dict) else None


def normalize_selector(selector: dict[str, Any] | str) -> dict[str, Any]:
    if isinstance(selector, str):
        return {"text": selector}
    normalized = dict(selector)
    if "runtime_id" in normalized and "id" not in normalized:
        normalized["id"] = normalized["runtime_id"]
    if "semantic_id" in normalized and "automation_id" not in normalized:
        normalized["automation_id"] = normalized["semantic_id"]
    return normalized


def node_matches(node: dict[str, Any], selector: dict[str, Any]) -> bool:
    node_id = str(node.get("id", ""))
    automation_id = str(node.get("automation_id", ""))
    text = str(node.get("label") or node.get("text", ""))

    if "id" in selector:
        wanted = str(selector["id"])
        if node_id != wanted and automation_id != wanted:
            return False
    if "automation_id" in selector and automation_id != str(selector["automation_id"]):
        return False
    if "role" in selector and str(node.get("role", "")) != str(selector["role"]):
        return False
    if "text" in selector and text != _clean_text(selector["text"]):
        return False
    if (
        "text_contains" in selector
        and str(selector["text_contains"]).lower() not in _node_search_text(node).lower()
    ):
        return False
    if "enabled" in selector and bool(node.get("enabled")) != bool(selector["enabled"]):
        return False
    if "checked" in selector and bool(node.get("checked")) != bool(selector["checked"]):
        return False
    if "focused" in selector and bool(node.get("focused")) != bool(selector["focused"]):
        return False
    return True


def _compact_node(node: dict[str, Any]) -> dict[str, Any]:
    compact = {
        "kind": node.get("kind", node.get("role", "")),
        "role": node.get("role", ""),
        "label": node.get("label") or node.get("text", ""),
        "automation_id": node.get("automation_id", ""),
        "actions": node.get("actions", []),
    }
    if node.get("automation_id"):
        compact["semantic_id"] = node.get("automation_id", "")
    if node.get("id"):
        compact["runtime_id"] = node.get("id", "")
    if node.get("focused"):
        compact["focused"] = True
    if node.get("checked"):
        compact["checked"] = True
    if node.get("enabled") is False:
        compact["enabled"] = False
    if _node_can_scroll(node):
        scroll = node.get("scroll", {})
        compact["scroll"] = {
            key: scroll.get(key)
            for key in (
                "can_scroll_up",
                "can_scroll_down",
                "can_scroll_left",
                "can_scroll_right",
                "top",
                "bottom",
                "left",
                "right",
            )
            if key in scroll
        }
    return compact


def _clean_text(value: Any) -> str:
    return " ".join(str(value or "").replace("\x00", " ").split())


def _normalized_node(node: dict[str, Any]) -> dict[str, Any]:
    normalized = dict(node)
    label = _clean_text(node.get("text", ""))
    normalized["text"] = label
    normalized["label"] = label
    normalized["kind"] = str(node.get("role", ""))
    if node.get("automation_id"):
        normalized["semantic_id"] = str(node.get("automation_id", ""))
    return normalized


def _node_search_text(node: dict[str, Any]) -> str:
    return " ".join(
        str(node.get(key, ""))
        for key in ("label", "text", "automation_id", "semantic_id", "role", "kind", "id")
        if node.get(key)
    )


def _action_warning(node: dict[str, Any]) -> dict[str, str]:
    label = str(node.get("label") or node.get("text") or "").lower()
    if "press any key" in label:
        return {
            "warning": "This dialog asks for a radio key; use edgetx_skip_storage_warning_if_present or edgetx_press ENTER if a pointer click does not dismiss it."
        }
    return {}


def _focused_node(tree: dict[str, Any]) -> dict[str, Any] | None:
    focused_id = tree.get("focused", "")
    if not focused_id:
        return None
    return next((n for n in tree.get("nodes", []) if n.get("id") == focused_id), None)


def _header_title(nodes: list[dict[str, Any]]) -> str:
    has_header_subtitle = False
    header_candidates: list[str] = []
    for node in nodes:
        label = str(node.get("label") or node.get("text") or "")
        bounds = node.get("bounds", [])
        if node.get("role") != "text" or not label or len(bounds) < 4:
            continue
        x, y, _, height = [int(v) for v in bounds[:4]]
        if x < 45 or y < 0 or y + height > 50:
            continue
        if label.lower() in ("quick menu", "back"):
            continue
        if y >= 18:
            has_header_subtitle = True
        else:
            header_candidates.append(label)
    return header_candidates[0] if has_header_subtitle and header_candidates else ""


def _current_page(tree: dict[str, Any]) -> dict[str, Any] | None:
    nodes = tree.get("nodes", [])
    pages = [n for n in nodes if n.get("role") in ("page", "dialog")]
    if pages:
        top_window = str(tree.get("top_window", ""))
        top_page = next((n for n in pages if n.get("id") == top_window), None)
        page = top_page or pages[-1]
        label = str(page.get("label") or page.get("text") or "") or _header_title(nodes)
        result = _compact_node(page)
        if label:
            result["label"] = label
        return result
    title = _header_title(nodes)
    if title:
        return {"kind": "page", "label": title, "actions": []}
    return None


def _blocking_dialog_context(nodes: list[dict[str, Any]]) -> dict[str, Any] | None:
    has_dialog = any(node.get("role") == "dialog" for node in nodes)
    if not has_dialog:
        return None

    text = [_clean_text(node.get("label") or node.get("text")) for node in nodes]
    text = [item for item in text if item]
    joined = " ".join(text).lower()
    if "storage warning" not in joined and "press any key" not in joined:
        return None

    title = next((item for item in text if "warning" in item.lower()), text[0] if text else "Dialog")
    message = next(
        (
            item
            for item in text
            if item != title and "press any key" not in item.lower() and "quick menu" not in item.lower()
        ),
        "",
    )
    action = next((node for node in nodes if node.get("automation_id") == "dialog.action"), None)
    result = {
        "type": "blocking_dialog",
        "title": title,
        "message": message,
        "instruction": "Use edgetx_skip_storage_warning_if_present to dismiss startup storage warnings; edgetx_press ENTER is the equivalent radio key fallback.",
        "skip_tool": "edgetx_skip_storage_warning_if_present",
    }
    if action:
        result["action"] = _compact_node(action)
    return result


def _is_storage_warning_blocker(blocker: dict[str, Any] | None) -> bool:
    if not blocker or blocker.get("type") != "blocking_dialog":
        return False
    text = f"{blocker.get('title', '')} {blocker.get('message', '')}".lower()
    return "storage warning" in text or blocker.get("skip_tool") == "edgetx_skip_storage_warning_if_present"


def _node_can_scroll(node: dict[str, Any], direction: str | None = None) -> bool:
    scroll = node.get("scroll", {})
    if not scroll or not scroll.get("scrollable"):
        return False
    if direction is None:
        return any(
            bool(scroll.get(key))
            for key in (
                "can_scroll_up",
                "can_scroll_down",
                "can_scroll_left",
                "can_scroll_right",
            )
        )
    return bool(scroll.get(f"can_scroll_{direction}"))


def _scrollable_nodes(nodes: list[dict[str, Any]], direction: str | None) -> list[dict[str, Any]]:
    return [n for n in nodes if _node_can_scroll(n, direction)]


def _meaningful_scrollable(node: dict[str, Any]) -> bool:
    if not _node_can_scroll(node):
        return False
    if str(node.get("role", "")) in ("text", "image", "button"):
        return False
    if _node_area(node) < 6000:
        return False
    scroll = node.get("scroll", {})
    return max(
        int(scroll.get("top", 0)),
        int(scroll.get("bottom", 0)),
        int(scroll.get("left", 0)),
        int(scroll.get("right", 0)),
    ) >= 24


def _screen_scrollables(nodes: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return sorted((n for n in nodes if _meaningful_scrollable(n)), key=_node_area, reverse=True)


def _node_area(node: dict[str, Any]) -> int:
    bounds = node.get("bounds", [])
    if len(bounds) < 4:
        return 0
    return max(0, int(bounds[2])) * max(0, int(bounds[3]))


def _scroll_drag_points(node: dict[str, Any], direction: str, amount: str) -> tuple[int, int, int, int]:
    bounds = node.get("bounds", [])
    if len(bounds) < 4:
        raise HarnessError("scroll target has no bounds")
    x, y, width, height = [int(v) for v in bounds[:4]]
    inset_x = min(24, max(4, width // 5))
    inset_y = min(24, max(4, height // 5))
    usable_width = max(1, width - 2 * inset_x)
    usable_height = max(1, height - 2 * inset_y)
    distance_scale = 0.75 if amount == "page" else 0.35
    dx = max(8, int(usable_width * distance_scale))
    dy = max(8, int(usable_height * distance_scale))
    cx = x + width // 2
    cy = y + height // 2

    if direction == "down":
        return cx, min(y + height - inset_y, cy + dy // 2), cx, max(y + inset_y, cy - dy // 2)
    if direction == "up":
        return cx, max(y + inset_y, cy - dy // 2), cx, min(y + height - inset_y, cy + dy // 2)
    if direction == "right":
        return min(x + width - inset_x, cx + dx // 2), cy, max(x + inset_x, cx - dx // 2), cy
    return max(x + inset_x, cx - dx // 2), cy, min(x + width - inset_x, cx + dx // 2), cy


def _viewport_scroll_drag_points(tree: dict[str, Any], direction: str, amount: str) -> tuple[int, int, int, int]:
    screen_node = next((n for n in tree.get("nodes", []) if n.get("id") == tree.get("screen")), None)
    bounds = screen_node.get("bounds", [0, 0, 480, 272]) if screen_node else [0, 0, 480, 272]
    x, y, width, height = [int(v) for v in bounds[:4]]
    header_height = min(45, max(0, height // 5))
    footer_height = min(10, max(0, height // 20))
    top = y + header_height
    bottom = y + height - footer_height
    left = x
    right = x + width
    distance_scale = 0.80 if amount == "page" else 0.35
    cx = left + width // 2
    cy = top + max(1, bottom - top) // 2
    dx = max(12, int((right - left) * distance_scale))
    dy = max(12, int((bottom - top) * distance_scale))

    if direction == "down":
        return cx, bottom - 12, cx, max(top + 12, bottom - 12 - dy)
    if direction == "up":
        return cx, top + 12, cx, min(bottom - 12, top + 12 + dy)
    if direction == "right":
        return right - 16, cy, max(left + 16, right - 16 - dx), cy
    return left + 16, cy, min(right - 16, left + 16 + dx), cy


def _available_inputs(nodes: list[dict[str, Any]]) -> list[dict[str, str]]:
    meaningful_scrollables = _screen_scrollables(nodes)
    inputs = [
        {"input": "ENTER", "effect": "activate_focused"},
        {"input": "EXIT", "effect": "back_or_dismiss"},
        {"input": "ROTARY", "effect": "move_focus_or_adjust"},
        {"input": "PAGEUP", "effect": "previous_page_or_tab"},
        {"input": "PAGEDN", "effect": "next_page_or_tab"},
        {"input": "TOUCH", "effect": "tap_visible_control"},
    ]
    if any(_node_can_scroll(n, "down") for n in meaningful_scrollables):
        inputs.append({"input": "TOUCH_DRAG_UP", "effect": "scroll_down"})
    if any(_node_can_scroll(n, "up") for n in meaningful_scrollables):
        inputs.append({"input": "TOUCH_DRAG_DOWN", "effect": "scroll_up"})
    if any(_node_can_scroll(n, "right") for n in meaningful_scrollables):
        inputs.append({"input": "TOUCH_DRAG_LEFT", "effect": "scroll_right"})
    if any(_node_can_scroll(n, "left") for n in meaningful_scrollables):
        inputs.append({"input": "TOUCH_DRAG_RIGHT", "effect": "scroll_left"})
    return inputs


def _screen_context(
    title: str,
    page: dict[str, Any] | None,
    focused: dict[str, Any] | str,
    actions: list[dict[str, Any]],
    scrollables: list[dict[str, Any]],
    nodes: list[dict[str, Any]],
) -> dict[str, Any]:
    blocking_dialog = _blocking_dialog_context(nodes)
    if blocking_dialog:
        return blocking_dialog

    focused_label = focused.get("label", "") if isinstance(focused, dict) else ""
    page_label = str((page or {}).get("label", ""))
    action_labels = [str(action.get("label", "")) for action in actions if action.get("label")]
    if (title == "Quick menu" or page_label == "Quick menu") and len(action_labels) > 1:
        return {
            "type": "menu_grid",
            "surface": "Quick menu",
            "focused_action": focused_label,
            "instruction": "Activate a visible menu item to open it; scrolling the page body will not open the focused item.",
        }
    if scrollables:
        return {
            "type": "page_form",
            "page": page_label or title,
            "section": _form_section_label(title, page_label),
            "focused_field": focused_label,
            "instruction": "Use edgetx_scroll direction=down to reveal lower form sections, or activate visible controls directly.",
        }
    return {
        "type": "screen",
        "page": page_label or title,
        "focused_action": focused_label,
        "instruction": "Use visible actions or user-equivalent keys shown in available_inputs.",
    }


def _form_section_label(title: str, page_label: str) -> str:
    if page_label and title and page_label != title:
        return title
    return ""


def _next_actions(
    context: dict[str, Any],
    focused: dict[str, Any] | str,
    scrollables: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    actions = []
    if context.get("type") == "blocking_dialog":
        if context.get("skip_tool"):
            actions.append({
                "tool": context.get("skip_tool"),
                "reason": "Dismiss the visible startup storage warning dialog",
            })
        actions.append({
            "tool": "edgetx_press",
            "key": "ENTER",
            "reason": "Equivalent radio key for dialogs that say Press any key",
        })
        dialog_action = context.get("action", {})
        if "click" in dialog_action.get("actions", []):
            actions.append({
                "tool": "edgetx_activate",
                "runtime_id": dialog_action.get("runtime_id", ""),
                "reason": "Pointer fallback for the visible dialog action",
            })
        return [action for action in actions if action.get("runtime_id", True)]

    if context.get("type") == "menu_grid" and isinstance(focused, dict) and "click" in focused.get("actions", []):
        actions.append({
            "tool": "edgetx_activate",
            "runtime_id": focused.get("runtime_id", ""),
            "reason": f"Open focused menu item {focused.get('label', '')}",
        })
        actions.append({
            "tool": "edgetx_press",
            "key": "ENTER",
            "reason": "Equivalent radio key to activate the focused item",
        })
    if context.get("type") == "page_form":
        if any(s.get("scroll", {}).get("can_scroll_down") for s in scrollables):
            actions.append({
                "tool": "edgetx_scroll",
                "direction": "down",
                "reason": "Reveal lower form sections in the visible page body",
            })
        if isinstance(focused, dict) and "click" in focused.get("actions", []):
            actions.append({
                "tool": "edgetx_activate",
                "runtime_id": focused.get("runtime_id", ""),
                "reason": f"Edit focused field {focused.get('label', '')}",
            })
    return [action for action in actions if action.get("runtime_id", True)]


def _filter_nodes(
    nodes: list[dict[str, Any]],
    actionable_only: bool,
    text_contains: str | None,
    limit: int,
) -> list[dict[str, Any]]:
    filtered = []
    needle = text_contains.lower() if text_contains else ""
    for node in nodes:
        if actionable_only and not node.get("actions"):
            continue
        if needle and needle not in _node_search_text(node).lower():
            continue
        filtered.append(node)
        if limit > 0 and len(filtered) >= limit:
            break
    return filtered


def _screen_digest(tree: dict[str, Any]) -> str:
    visible = [
        f"{n.get('role','?')}:{n.get('text','') or n.get('automation_id','')}:{','.join(n.get('actions', []))}"
        for n in tree.get("nodes", [])
        if n.get("actions") or n.get("text") or n.get("automation_id")
    ]
    return hashlib.sha256("\n".join(visible[:200]).encode("utf-8")).hexdigest()[:16]


def summarize_ui_nodes(nodes: list[dict[str, Any]], limit: int = 40) -> str:
    lines = []
    for node in nodes:
        text = str(node.get("text", ""))
        automation_id = str(node.get("automation_id", ""))
        actions = ",".join(node.get("actions", []))
        if not text and not automation_id and not actions:
            continue
        bounds = node.get("bounds", [])
        lines.append(
            f"- role={node.get('role', '')} id={automation_id or node.get('id', '')} "
            f"text={text!r} actions=[{actions}] bounds={bounds}"
        )
        if len(lines) >= limit:
            break
    return "\n".join(lines) if lines else "(no visible labeled/actionable nodes)"


def default_fixture_path(kind: str, target: str) -> Path:
    return REPO_ROOT / "tools" / "ui-harness" / "fixtures" / f"{kind}-{target}"


def normalize_yaml_line_endings(base: Path) -> None:
    for path in base.rglob("*.yml"):
        data = path.read_bytes()
        normalized = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
        path.write_bytes(normalized.replace(b"\n", b"\r\n"))


class HarnessService:
    def __init__(self) -> None:
        self.session: SdlAutomationSession | None = None

    def build(self, target: str = "tx16s") -> dict[str, Any]:
        return build_simulator(target)

    def start(
        self,
        target: str = "tx16s",
        sdcard: str | None = None,
        settings: str | None = None,
    ) -> dict[str, Any]:
        self.stop()
        self.session = SdlAutomationSession(
            target=target,
            sdcard=Path(sdcard) if sdcard else None,
            settings=Path(settings) if settings else None,
        )
        return self.session.start()

    def stop(self) -> dict[str, Any]:
        if not self.session:
            return {"running": False}
        result = self.session.stop()
        self.session = None
        return result

    def require_session(self) -> SdlAutomationSession:
        if not self.session:
            raise HarnessError("simulator has not been started")
        return self.session

    def status(self) -> dict[str, Any]:
        return self.require_session().status()

    def press(self, key: str, duration_ms: int = 120) -> dict[str, Any]:
        return self.require_session().press(key, duration_ms)

    def long_press(self, key: str, duration_ms: int = 800) -> dict[str, Any]:
        return self.require_session().long_press(key, duration_ms)

    def rotate(self, steps: int) -> dict[str, Any]:
        return self.require_session().rotate(steps)

    def touch(self, x: int, y: int, duration_ms: int = 120) -> dict[str, Any]:
        return self.require_session().touch(x, y, duration_ms)

    def drag(
        self,
        x1: int,
        y1: int,
        x2: int,
        y2: int,
        duration_ms: int = 300,
        steps: int = 12,
    ) -> dict[str, Any]:
        return self.require_session().drag(x1, y1, x2, y2, duration_ms, steps)

    def scroll(
        self,
        direction: str,
        target: dict[str, Any] | str | None = None,
        amount: str = "page",
        duration_ms: int = 300,
    ) -> dict[str, Any]:
        return self.require_session().scroll(direction, target, amount, duration_ms)

    def wait(self, ms: int) -> dict[str, Any]:
        return self.require_session().wait(ms)

    def set_telemetry(self, name: str, value: float) -> dict[str, Any]:
        return self.require_session().set_telemetry(name, value)

    def set_telemetry_streaming(self, enabled: bool) -> dict[str, Any]:
        return self.require_session().set_telemetry_streaming(enabled)

    def set_switch(self, index: int, position: int) -> dict[str, Any]:
        return self.require_session().set_switch(index, position)

    def switch_sequence(self, steps: list[dict[str, int]]) -> dict[str, Any]:
        return self.require_session().switch_sequence(steps)

    def audio_history(self, max_lines: int = 200) -> dict[str, Any]:
        return self.require_session().audio_history(max_lines)

    def ui_tree(
        self,
        mode: str = "full",
        actionable_only: bool = False,
        text_contains: str | None = None,
        limit: int = 0,
        verbose: bool = True,
    ) -> dict[str, Any]:
        return self.require_session().ui_tree(mode, actionable_only, text_contains, limit, verbose)

    def screen(self) -> dict[str, Any]:
        return self.require_session().screen()

    def wait_for(
        self,
        text_contains: str | None = None,
        automation_id: str | None = None,
        role: str | None = None,
        timeout_ms: int = 2000,
        poll_ms: int = 200,
    ) -> dict[str, Any]:
        return self.require_session().wait_for(text_contains, automation_id, role, timeout_ms, poll_ms)

    def ui_click(self, selector: dict[str, Any] | str, duration_ms: int = 0, verbose: bool = False) -> dict[str, Any]:
        return self.require_session().ui_click(selector, duration_ms, verbose)

    def ui_long_click(self, selector: dict[str, Any] | str, duration_ms: int = 0, verbose: bool = False) -> dict[str, Any]:
        return self.require_session().ui_long_click(selector, duration_ms, verbose)

    def activate(
        self,
        selector: dict[str, Any] | str,
        action: str = "click",
        duration_ms: int = 0,
        verbose: bool = False,
    ) -> dict[str, Any]:
        return self.require_session().activate(selector, action, duration_ms, verbose)

    def assert_visible(self, selector: dict[str, Any] | str, verbose: bool = False) -> dict[str, Any]:
        node = self.require_session().find_node(selector)
        if verbose:
            return {"matched": node}
        return {"matched": _compact_node(node)}

    def skip_storage_warning_if_present(self) -> dict[str, Any]:
        return self.require_session().skip_storage_warning_if_present()

    def screenshot(self, name: str, out_dir: str | None = None) -> dict[str, Any]:
        output = Path(out_dir) if out_dir else REPO_ROOT / "build" / "ui-harness" / "screenshots"
        return self.require_session().screenshot(name, output)

    def run_flow(self, flow_path: str) -> dict[str, Any]:
        flow = json.loads(Path(flow_path).read_text())
        target = flow.get("target", "tx16s")
        output_dir = Path(flow.get("output", REPO_ROOT / "build" / "ui-harness" / "screenshots" / Path(flow_path).stem))
        self.start(target, flow.get("sdcard"), flow.get("settings"))
        screenshots = []
        failed_step = -1
        try:
            for index, step in enumerate(flow.get("steps", [])):
                failed_step = index
                self._run_step(step, output_dir, screenshots)
            return {"flow": flow_path, "screenshots": screenshots, "output": str(output_dir)}
        except Exception as exc:
            return {
                "flow": flow_path,
                "error": str(exc),
                "screenshots": screenshots,
                "failed_step": failed_step,
                "output": str(output_dir),
            }
        finally:
            self.stop()

    def _run_step(self, step: dict[str, Any], output_dir: Path, screenshots: list[dict[str, Any]]) -> None:
        if "wait" in step:
            value = step["wait"]
            self.wait(int(value.get("ms", 250) if isinstance(value, dict) else value))
        elif "ui_tree" in step:
            self.ui_tree()
        elif "click" in step:
            value = step["click"]
            if isinstance(value, str):
                self.ui_click(value)
            else:
                selector = value.get("selector", value)
                self.ui_click(selector, int(value.get("duration_ms", 0)))
        elif "long_click" in step:
            value = step["long_click"]
            if isinstance(value, str):
                self.ui_long_click(value)
            else:
                selector = value.get("selector", value)
                self.ui_long_click(selector, int(value.get("duration_ms", 0)))
        elif "assert_visible" in step:
            self.assert_visible(step["assert_visible"])
        elif "skip_storage_warning_if_present" in step:
            self.skip_storage_warning_if_present()
        elif step.get("invoke") == "skip_storage_warning_if_present":
            self.skip_storage_warning_if_present()
        elif "press" in step:
            value = step["press"]
            if isinstance(value, str):
                self.press(value)
            else:
                self.press(value["key"], int(value.get("duration_ms", 120)))
        elif "long_press" in step:
            value = step["long_press"]
            if isinstance(value, str):
                self.long_press(value)
            else:
                self.long_press(value["key"], int(value.get("duration_ms", 800)))
        elif "rotate" in step:
            self.rotate(int(step["rotate"]))
        elif "touch" in step:
            value = step["touch"]
            self.touch(int(value["x"]), int(value["y"]), int(value.get("duration_ms", 120)))
        elif "drag" in step:
            value = step["drag"]
            self.drag(
                int(value["x1"]),
                int(value["y1"]),
                int(value["x2"]),
                int(value["y2"]),
                int(value.get("duration_ms", 300)),
                int(value.get("steps", 12)),
            )
        elif "switch_sequence" in step:
            self.switch_sequence(step["switch_sequence"])
        elif "screenshot" in step:
            screenshots.append(self.screenshot(str(step["screenshot"]), str(output_dir)))
        else:
            raise HarnessError(f"unknown flow step: {step}")
