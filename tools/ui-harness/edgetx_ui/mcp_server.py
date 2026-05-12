import json
import sys
from typing import Any

from .core import HarnessError, HarnessService


TOOLS: dict[str, dict[str, Any]] = {
    "edgetx_build_simulator": {
        "description": "Configure and build a simulator target.",
        "inputSchema": {"type": "object", "properties": {"target": {"type": "string", "default": "tx16s"}}},
    },
    "edgetx_start_simulator": {
        "description": "Start a simulator session with optional storage fixtures.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "target": {"type": "string", "default": "tx16s"},
                "sdcard": {"type": "string"},
                "settings": {"type": "string"},
            },
        },
    },
    "edgetx_stop_simulator": {"description": "Stop the active simulator session.", "inputSchema": {"type": "object"}},
    "edgetx_status": {
        "description": "Return simulator status and LCD geometry. If startup is blocked by a visible warning dialog, startup_blocker includes the suggested skip tool.",
        "inputSchema": {"type": "object", "properties": {"compact": {"type": "boolean", "default": False}}},
    },
    "edgetx_press": {
        "description": "Press a named radio key.",
        "inputSchema": {
            "type": "object",
            "properties": {"key": {"type": "string"}, "duration_ms": {"type": "integer", "default": 120}},
            "required": ["key"],
        },
    },
    "edgetx_long_press": {
        "description": "Long press a named radio key.",
        "inputSchema": {
            "type": "object",
            "properties": {"key": {"type": "string"}, "duration_ms": {"type": "integer", "default": 800}},
            "required": ["key"],
        },
    },
    "edgetx_rotate": {
        "description": "Send rotary encoder steps.",
        "inputSchema": {"type": "object", "properties": {"steps": {"type": "integer"}}, "required": ["steps"]},
    },
    "edgetx_touch": {
        "description": "Tap the touch screen.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "x": {"type": "integer"},
                "y": {"type": "integer"},
                "duration_ms": {"type": "integer", "default": 120},
            },
            "required": ["x", "y"],
        },
    },
    "edgetx_drag": {
        "description": "Drag across the touch screen.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "x1": {"type": "integer"},
                "y1": {"type": "integer"},
                "x2": {"type": "integer"},
                "y2": {"type": "integer"},
                "duration_ms": {"type": "integer", "default": 300},
                "steps": {"type": "integer", "default": 12},
            },
            "required": ["x1", "y1", "x2", "y2"],
        },
    },
    "edgetx_scroll": {
        "description": "Perform a user-equivalent touch drag on the visible content viewport by default, or on an explicit node selector. Does not set LVGL scroll position directly or navigate hidden screens.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "direction": {"type": "string", "enum": ["up", "down", "left", "right"]},
                "amount": {"type": "string", "enum": ["small", "page"], "default": "page"},
                "target": {"type": "object"},
                "id": {"type": "string"},
                "runtime_id": {"type": "string"},
                "semantic_id": {"type": "string"},
                "automation_id": {"type": "string"},
                "role": {"type": "string"},
                "text": {"type": "string"},
                "text_contains": {"type": "string"},
                "duration_ms": {"type": "integer", "default": 300},
            },
            "required": ["direction"],
        },
    },
    "edgetx_wait": {
        "description": "Wait for a number of milliseconds.",
        "inputSchema": {"type": "object", "properties": {"ms": {"type": "integer"}}, "required": ["ms"]},
    },
    "edgetx_ui_tree": {
        "description": "Return the LVGL UI tree. Default mode=summary returns labeled/actionable nodes with compact fields. mode=full returns the full tree. Use actionable_only, text_contains, limit to filter.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "mode": {"type": "string", "enum": ["summary", "full"], "default": "summary"},
                "actionable_only": {"type": "boolean", "default": False},
                "text_contains": {"type": "string"},
                "limit": {"type": "integer", "default": 0},
                "verbose": {"type": "boolean", "default": False},
            },
        },
    },
    "edgetx_screen": {
        "description": "Low-token accessibility-style summary: current page/title, context, suggested next actions, focused node, visible user-actionable controls, available user-equivalent inputs, and visible text.",
        "inputSchema": {"type": "object"},
    },
    "edgetx_activate": {
        "description": "Activate a currently visible user-actionable UI node by semantic_id/automation_id, id/runtime_id, role, text, or text_contains. This does not navigate or infer targets; it only invokes declared click/long_click actions.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "selector": {"type": "object"},
                "semantic_id": {"type": "string"},
                "automation_id": {"type": "string"},
                "id": {"type": "string"},
                "runtime_id": {"type": "string"},
                "role": {"type": "string"},
                "text": {"type": "string"},
                "text_contains": {"type": "string"},
                "action": {"type": "string", "enum": ["click", "long_click"], "default": "click"},
                "duration_ms": {"type": "integer", "default": 0},
                "verbose": {"type": "boolean", "default": False},
            },
        },
    },
    "edgetx_click": {
        "description": "Invoke a visible UI node click action by text, role, id, automation_id, or text_contains. Returns compact result by default (ok + matched node summary). Use verbose=true for full node.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "selector": {"type": "object"},
                "text": {"type": "string"},
                "text_contains": {"type": "string"},
                "role": {"type": "string"},
                "id": {"type": "string"},
                "automation_id": {"type": "string"},
                "duration_ms": {"type": "integer", "default": 0},
                "verbose": {"type": "boolean", "default": False},
            },
        },
    },
    "edgetx_long_click": {
        "description": "Invoke a visible UI node long-click action by text, role, id, automation_id, or text_contains. Returns compact result by default.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "selector": {"type": "object"},
                "text": {"type": "string"},
                "text_contains": {"type": "string"},
                "role": {"type": "string"},
                "id": {"type": "string"},
                "automation_id": {"type": "string"},
                "duration_ms": {"type": "integer", "default": 0},
                "verbose": {"type": "boolean", "default": False},
            },
        },
    },
    "edgetx_assert_visible": {
        "description": "Assert that a UI node selected by text, role, id, automation_id, or text_contains is visible. Returns compact result by default.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "selector": {"type": "object"},
                "text": {"type": "string"},
                "text_contains": {"type": "string"},
                "role": {"type": "string"},
                "id": {"type": "string"},
                "automation_id": {"type": "string"},
                "verbose": {"type": "boolean", "default": False},
            },
        },
    },
    "edgetx_wait_for": {
        "description": "Poll until a node matching text_contains, automation_id, or role appears. Returns found=true and the node, or found=false on timeout. Combines wait+ui_tree+assert into one call.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "text_contains": {"type": "string"},
                "automation_id": {"type": "string"},
                "role": {"type": "string"},
                "timeout_ms": {"type": "integer", "default": 2000},
                "poll_ms": {"type": "integer", "default": 200},
            },
        },
    },
    "edgetx_skip_storage_warning_if_present": {
        "description": "If a storage warning dialog is visible, dismiss it with bounded user-equivalent ENTER/action attempts and report the final blocker if it remains.",
        "inputSchema": {"type": "object"},
    },
    "edgetx_screenshot": {
        "description": "Capture a framebuffer screenshot as PNG.",
        "inputSchema": {
            "type": "object",
            "properties": {"name": {"type": "string"}, "out_dir": {"type": "string"}},
            "required": ["name"],
        },
    },
    "edgetx_set_telemetry": {
        "description": "Inject a telemetry sensor value. name is the sensor label (e.g. 'VFAS', 'Capa') and value is the raw integer value (VFAS: volts*100, Capa: mAh).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Sensor label, e.g. 'VFAS' or 'Capa'"},
                "value": {"type": "number", "description": "Raw integer value (VFAS: volts*100, Capa: mAh)"},
            },
            "required": ["name", "value"],
        },
    },
    "edgetx_set_telemetry_streaming": {
        "description": "Enable or disable telemetry streaming simulation. When enabled, Lua scripts will see TELEMETRY_STREAMING() as true, allowing telemetry sensors to be readable.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "enabled": {"type": "boolean", "description": "True to enable telemetry streaming, False to disable"},
            },
            "required": ["enabled"],
        },
    },
    "edgetx_set_switch": {
        "description": "Set a radio switch position by index. index is the 0-based switch index (SA=0, SB=1, ..., SF=5, SH=7). position is -1 (down), 0 (middle), or +1 (up).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "index": {"type": "integer", "description": "0-based switch index: SA=0, SB=1, SC=2, SD=3, SE=4, SF=5, SG=6, SH=7"},
                "position": {"type": "integer", "description": "Switch position: -1 (down/back), 0 (middle), +1 (up/forward)"},
            },
            "required": ["index", "position"],
        },
    },
    "edgetx_switch_sequence": {
        "description": "Run a deterministic switch sequence. Each step sets a switch position, then holds it for duration_ms before the next step.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "steps": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "index": {"type": "integer", "description": "0-based switch index: SA=0, SB=1, SC=2, SD=3, SE=4, SF=5, SG=6, SH=7"},
                            "position": {"type": "integer", "description": "Switch position: -1 (down/back), 0 (middle), +1 (up/forward)"},
                            "duration_ms": {"type": "integer", "default": 0, "description": "Time to hold this position before the next step"},
                        },
                        "required": ["index", "position"],
                    },
                    "minItems": 1,
                    "maxItems": 16,
                },
            },
            "required": ["steps"],
        },
    },
    "edgetx_audio_history": {
        "description": "Return captured simulator audio events (playTone, playFile calls).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "tail": {"type": "integer", "default": 200, "description": "Maximum number of tail lines to return"},
            },
        },
    },
    "edgetx_run_flow": {
        "description": "Run a JSON UI flow.",
        "inputSchema": {"type": "object", "properties": {"flow_path": {"type": "string"}}, "required": ["flow_path"]},
    },
    "edgetx_log": {
        "description": "Return captured simulator log lines (TRACE/debug output).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "filter": {"type": "string", "description": "Substring to filter log lines by"},
                "tail": {"type": "integer", "default": 200, "description": "Maximum number of tail lines to return"},
            },
        },
    },
}


def selector_from_args(args: dict[str, Any]) -> dict[str, Any]:
    selector = dict(args.get("selector") or {})
    for key in ("id", "runtime_id", "semantic_id", "automation_id", "role", "text", "text_contains", "enabled", "checked", "focused", "index"):
        if key in args:
            selector[key] = args[key]
    return selector


LARGE_RESULT_TOOLS = {
    "edgetx_ui_tree",
    "edgetx_screen",
    "edgetx_screenshot",
    "edgetx_log",
    "edgetx_audio_history",
    "edgetx_run_flow",
}


def _format_result(result: dict[str, Any], tool_name: str) -> str:
    if tool_name in LARGE_RESULT_TOOLS:
        return json.dumps(result, separators=(",", ":"))
    return json.dumps(result, indent=2)


class McpServer:
    def __init__(self) -> None:
        self.service = HarnessService()

    def handle(self, request: dict[str, Any]) -> dict[str, Any] | None:
        method = request.get("method")
        request_id = request.get("id")
        try:
            if method == "initialize":
                return self.response(request_id, {"protocolVersion": "2024-11-05", "serverInfo": {"name": "edgetx-ui-harness", "version": "0.2.0"}, "capabilities": {"tools": {}}})
            if method == "notifications/initialized":
                return None
            if method == "tools/list":
                return self.response(request_id, {"tools": [{"name": name, **schema} for name, schema in TOOLS.items()]})
            if method == "tools/call":
                params = request.get("params") or {}
                result = self.call_tool(params.get("name"), params.get("arguments") or {})
                return self.response(request_id, {"content": [{"type": "text", "text": _format_result(result, params.get("name", ""))}]})
            return self.error(request_id, -32601, f"unknown method: {method}")
        except HarnessError as exc:
            return self.error(request_id, -32000, str(exc))
        except Exception as exc:
            return self.error(request_id, -32000, str(exc))

    def call_tool(self, name: str, args: dict[str, Any]) -> dict[str, Any]:
        if name == "edgetx_build_simulator":
            return self.service.build(args.get("target", "tx16s"))
        if name == "edgetx_start_simulator":
            return self.service.start(args.get("target", "tx16s"), args.get("sdcard"), args.get("settings"))
        if name == "edgetx_stop_simulator":
            return self.service.stop()
        if name == "edgetx_status":
            st = self.service.status()
            if args.get("compact"):
                return {k: v for k, v in st.items() if k not in ("sdcard", "settings")}
            return st
        if name == "edgetx_press":
            return self.service.press(args["key"], int(args.get("duration_ms", 120)))
        if name == "edgetx_long_press":
            return self.service.long_press(args["key"], int(args.get("duration_ms", 800)))
        if name == "edgetx_rotate":
            return self.service.rotate(int(args["steps"]))
        if name == "edgetx_touch":
            return self.service.touch(int(args["x"]), int(args["y"]), int(args.get("duration_ms", 120)))
        if name == "edgetx_drag":
            return self.service.drag(
                int(args["x1"]),
                int(args["y1"]),
                int(args["x2"]),
                int(args["y2"]),
                int(args.get("duration_ms", 300)),
                int(args.get("steps", 12)),
            )
        if name == "edgetx_scroll":
            selector = selector_from_args(args)
            return self.service.scroll(
                args["direction"],
                selector or None,
                args.get("amount", "page"),
                int(args.get("duration_ms", 300)),
            )
        if name == "edgetx_wait":
            return self.service.wait(int(args["ms"]))
        if name == "edgetx_ui_tree":
            return self.service.ui_tree(
                mode=args.get("mode", "summary"),
                actionable_only=args.get("actionable_only", False),
                text_contains=args.get("text_contains"),
                limit=int(args.get("limit", 0)),
                verbose=bool(args.get("verbose", False)),
            )
        if name == "edgetx_screen":
            return self.service.screen()
        if name == "edgetx_activate":
            return self.service.activate(
                selector_from_args(args),
                args.get("action", "click"),
                int(args.get("duration_ms", 0)),
                bool(args.get("verbose", False)),
            )
        if name == "edgetx_click":
            return self.service.ui_click(
                selector_from_args(args),
                int(args.get("duration_ms", 0)),
                bool(args.get("verbose", False)),
            )
        if name == "edgetx_long_click":
            return self.service.ui_long_click(
                selector_from_args(args),
                int(args.get("duration_ms", 0)),
                bool(args.get("verbose", False)),
            )
        if name == "edgetx_assert_visible":
            return self.service.assert_visible(selector_from_args(args), bool(args.get("verbose", False)))
        if name == "edgetx_wait_for":
            return self.service.wait_for(
                text_contains=args.get("text_contains"),
                automation_id=args.get("automation_id"),
                role=args.get("role"),
                timeout_ms=int(args.get("timeout_ms", 2000)),
                poll_ms=int(args.get("poll_ms", 200)),
            )
        if name == "edgetx_skip_storage_warning_if_present":
            return self.service.skip_storage_warning_if_present()
        if name == "edgetx_screenshot":
            return self.service.screenshot(args["name"], args.get("out_dir"))
        if name == "edgetx_set_telemetry":
            return self.service.set_telemetry(args["name"], float(args["value"]))
        if name == "edgetx_set_telemetry_streaming":
            return self.service.set_telemetry_streaming(bool(args["enabled"]))
        if name == "edgetx_set_switch":
            return self.service.set_switch(int(args["index"]), int(args["position"]))
        if name == "edgetx_switch_sequence":
            return self.service.switch_sequence(args["steps"])
        if name == "edgetx_audio_history":
            return self.service.audio_history(int(args.get("tail", 200)))
        if name == "edgetx_run_flow":
            return self.service.run_flow(args["flow_path"])
        if name == "edgetx_log":
            lines = self.service.require_session().get_logs(filter_text=args.get("filter"), tail=int(args.get("tail", 200)))
            return {"lines": lines}
        raise HarnessError(f"unknown tool: {name}")

    @staticmethod
    def response(request_id: Any, result: dict[str, Any]) -> dict[str, Any]:
        return {"jsonrpc": "2.0", "id": request_id, "result": result}

    @staticmethod
    def error(request_id: Any, code: int, message: str) -> dict[str, Any]:
        return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


def main() -> int:
    server = McpServer()
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        response = server.handle(json.loads(line))
        if response is not None:
            print(json.dumps(response), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
