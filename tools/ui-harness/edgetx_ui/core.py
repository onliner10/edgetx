import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
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
        self.sdcard = Path(sdcard) if sdcard else self.runtime_fixture_path("sdcard")
        self.settings = Path(settings) if settings else self.runtime_fixture_path("settings")
        self.width = width
        self.height = height
        self.process: subprocess.Popen[str] | None = None

    def runtime_fixture_path(self, kind: str) -> Path:
        if self.runtime_dir is None:
            self.runtime_dir = tempfile.TemporaryDirectory(prefix=f"edgetx-ui-{self.target.name}-")

        source = default_fixture_path(kind, self.target.name)
        destination = Path(self.runtime_dir.name) / f"{kind}-{self.target.name}"
        if source.exists():
            shutil.copytree(source, destination, dirs_exist_ok=True)
            if kind == "settings":
                normalize_yaml_line_endings(destination)
        else:
            destination.mkdir(parents=True, exist_ok=True)
        return destination

    def start(self) -> dict[str, Any]:
        if self.process and self.process.poll() is None:
            return self.status()

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
        deadline = time.monotonic() + 5.0
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            try:
                return self.status()
            except HarnessError as exc:
                last_error = exc
                if self.process.poll() is not None:
                    break
                time.sleep(0.05)
        raise HarnessError(f"simulator did not become responsive: {last_error}")

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
        tail = "\n".join(recent_lines)
        if tail:
            raise HarnessError(
                f"timed out waiting for simulator response to `{command}`\nRecent output:\n{tail}"
            )
        raise HarnessError(f"timed out waiting for simulator response to `{command}`")

    def status(self) -> dict[str, Any]:
        response = self.command("status")
        return {
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

    def wait(self, ms: int) -> dict[str, Any]:
        return self.command(f"wait {int(ms)}")

    def ui_tree(self) -> dict[str, Any]:
        response = self.command("ui_tree", timeout=5.0)
        return response.get("ui", {"nodes": []})

    def find_node(self, selector: dict[str, Any] | str, action: str | None = None) -> dict[str, Any]:
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

        raise HarnessError(
            f"could not find UI node matching {json.dumps(normalized, sort_keys=True)}"
            + (f" with action `{required_action}`" if required_action else "")
            + "\nVisible nodes:\n"
            + summarize_ui_nodes(tree.get("nodes", []))
        )

    def ui_click(self, selector: dict[str, Any] | str, duration_ms: int = 0) -> dict[str, Any]:
        node = self.find_node(selector, "click")
        response = self.command(f"ui_click {node['id']} {int(duration_ms)}", timeout=5.0)
        return response | {"matched": node}

    def ui_long_click(self, selector: dict[str, Any] | str, duration_ms: int = 0) -> dict[str, Any]:
        node = self.find_node(selector, "long_click")
        response = self.command(f"ui_long_click {node['id']} {int(duration_ms)}", timeout=5.0)
        return response | {"matched": node}

    def assert_visible(self, selector: dict[str, Any] | str) -> dict[str, Any]:
        return {"matched": self.find_node(selector)}

    def skip_storage_warning_if_present(self) -> dict[str, Any]:
        results = []
        for _ in range(5):
            tree = self.ui_tree()
            warning_text = " ".join(str(node.get("text", "")) for node in tree.get("nodes", [])).lower()
            if "storage warning" not in warning_text and "press any key to skip" not in warning_text:
                return {"skipped": bool(results), "count": len(results), "results": results}

            try:
                results.append({"method": "dialog.action", "result": self.ui_click({"automation_id": "dialog.action"})})
            except HarnessError:
                self.press("ENTER")
                self.wait(500)
                results.append({"method": "ENTER"})

        raise HarnessError("storage warning still visible after 5 skip attempts")

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
    return dict(selector)


def node_matches(node: dict[str, Any], selector: dict[str, Any]) -> bool:
    node_id = str(node.get("id", ""))
    automation_id = str(node.get("automation_id", ""))
    text = str(node.get("text", ""))

    if "id" in selector:
        wanted = str(selector["id"])
        if node_id != wanted and automation_id != wanted:
            return False
    if "automation_id" in selector and automation_id != str(selector["automation_id"]):
        return False
    if "role" in selector and str(node.get("role", "")) != str(selector["role"]):
        return False
    if "text" in selector and text != str(selector["text"]):
        return False
    if "text_contains" in selector and str(selector["text_contains"]) not in text:
        return False
    if "enabled" in selector and bool(node.get("enabled")) != bool(selector["enabled"]):
        return False
    if "checked" in selector and bool(node.get("checked")) != bool(selector["checked"]):
        return False
    if "focused" in selector and bool(node.get("focused")) != bool(selector["focused"]):
        return False
    return True


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

    def wait(self, ms: int) -> dict[str, Any]:
        return self.require_session().wait(ms)

    def ui_tree(self) -> dict[str, Any]:
        return self.require_session().ui_tree()

    def ui_click(self, selector: dict[str, Any] | str, duration_ms: int = 0) -> dict[str, Any]:
        return self.require_session().ui_click(selector, duration_ms)

    def ui_long_click(self, selector: dict[str, Any] | str, duration_ms: int = 0) -> dict[str, Any]:
        return self.require_session().ui_long_click(selector, duration_ms)

    def assert_visible(self, selector: dict[str, Any] | str) -> dict[str, Any]:
        return self.require_session().assert_visible(selector)

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
        elif "screenshot" in step:
            screenshots.append(self.screenshot(str(step["screenshot"]), str(output_dir)))
        else:
            raise HarnessError(f"unknown flow step: {step}")
