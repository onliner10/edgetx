# EdgeTX UI Harness

This harness gives agents and CI a repeatable control plane for TX16S-class
simulator UI work.

The current backend starts the SDL simulator with `--automation-stdio`. The
automation protocol runs inside the simulator process and uses `simuSetKey`,
`simuRotaryEncoderEvent`, `simuTouchDown`, `simuTouchUp`, and `simuLcdCopy`.
Screenshots are copied from the simulator framebuffer, then converted to PNG by
the Python harness.

## Usage

```sh
nix develop
tools/ui-harness/edgetx-ui build tx16s
tools/ui-harness/edgetx-ui build tx16smk3
tools/ui-harness/edgetx-ui smoke --target tx16s
tools/ui-harness/edgetx-ui run-flow tools/ui-harness/flows/tx16s-smoke.json
tools/ui-harness/edgetx-mcp
```

`edgetx-mcp` is a stdio MCP server. It exposes build/start/stop, key, rotary,
touch, wait, screenshot, status, live UI-tree, screen summary, selector click,
selector long-click, visibility assertion, wait-for, storage-warning skip, and
run-flow tools. It uses the same Python core as the CLI.

### Agent-friendly patterns

Prefer `edgetx_screen` (low-token accessibility-style summary) or
`edgetx_ui_tree mode=summary` (labeled/actionable nodes only) over the full
tree. Click/assert results are compact by default; use `verbose=true` for full
node details. The MCP deliberately does not provide screen-jump navigation:
actions are limited to visible, user-reachable controls and radio inputs.

```text
edgetx_start_simulator target=tx16s
edgetx_status                                 # if startup_blocker is present, use its skip_tool
edgetx_skip_storage_warning_if_present        # bounded ENTER/action attempts for storage warnings
edgetx_screen                                # low-token: title, actions, visible_text
edgetx_ui_tree mode=summary actionable_only=true  # only clickable nodes
edgetx_activate automation_id=model.model2.yml
edgetx_scroll direction=down amount=page    # real touch-drag on visible scrollable content
edgetx_wait_for text_contains="Throttle" timeout_ms=3000
edgetx_screenshot name=after-model-click
```

### Tool reference

`edgetx_ui_tree` — LVGL tree. mode=summary (default for agents) returns
labeled/actionable nodes with compact fields. mode=full returns the full tree.
Use `actionable_only`, `text_contains`, `limit`, `verbose` to filter.

`edgetx_screen` — Low-token accessibility-style summary: `{title, page,
context, focused, screen_hash,
actions:[{kind,label,automation_id,semantic_id,actions}], scrollables,
available_inputs, next_actions, visible_text, counts}`. Text is normalized for
matching and display. Use this instead of ui_tree when you only need to know the
current page, whether you are on a menu grid or form, what is focused, what is
visible, which meaningful containers can scroll, and what user-equivalent
controls can be activated. Blocking startup dialogs report
`context.type=blocking_dialog`, include `skip_tool`, and put
`edgetx_skip_storage_warning_if_present` first in `next_actions`.

`edgetx_status` — Simulator status and LCD geometry. If startup is not complete
because a storage warning is visible, the result includes `startup_blocker` with
`skip_tool=edgetx_skip_storage_warning_if_present` so agents know how to proceed
without repeatedly clicking the dialog.

`edgetx_activate` — User-equivalent semantic activation of a currently visible
node. It accepts `semantic_id`, `automation_id`, `id`/`runtime_id`, `role`,
`text`, or `text_contains`, and invokes only a declared `click` or `long_click`
action. It does not infer nearby controls, walk to hidden screens, or perform
multi-step navigation. If a label is visible but not actionable, activation
fails with the visible node context. Dialog actions labeled "Press any key" warn
that `edgetx_skip_storage_warning_if_present` or `edgetx_press ENTER` may be
more reliable than pointer clicks.

`edgetx_scroll` — User-equivalent scroll gesture. By default it drags the main
visible content viewport, which avoids noisy LVGL internals such as tiny labels
that are technically scrollable. A selector can target a specific node when that
is under test. It reports the gesture and screen hashes; it does not call hidden
LVGL scroll setters or navigate to unreachable screens. If the gesture does not
change the screen, the result includes `changed=false` and a warning.

`edgetx_wait_for` — Poll until a node appears. Combines wait+ui_tree+assert.
Returns `found`, `node`, `screen_hash`, `changed` — avoids repeated
wait/tree/assert loops.

`edgetx_skip_storage_warning_if_present` — Bounded helper for startup storage
warnings. It prefers the radio-key path (`ENTER`), may try the visible dialog
action as a fallback, and returns `ok=false` plus `final_blocker` instead of
leaving agents to guess after repeated failed attempts.

`edgetx_click` / `edgetx_long_click` — Compact result by default:
`{ok, matched:{role,text,automation_id,actions,runtime_id}, runtime_id}`. Full
node with `verbose=true`.

`edgetx_status compact=true` — Removes temporary `sdcard` and `settings` paths
from status output.

JSON is compact (no indentation) for large results (ui_tree, screen, screenshot,
log, audio_history, run_flow). Other tools return pretty JSON.

Selector preference: `automation_id` > exact `text` > `text_contains` >
`role` > raw coordinates. `id` refers to the runtime pointer (lv:...).
`semantic_id` is an alias for `automation_id` when present, and `runtime_id` is
an alias for `id`. Selector `activate`, `click`, and `long_click` invoke on the
menu/UI thread without fixed sleeps; raw `touch` and `drag` remain available
when touch timing is under test. JSON flows are best treated as replay artifacts
after the interactive path is known.

Default sessions copy these fixtures into a temporary runtime directory before
starting the simulator, so smoke runs do not modify tracked fixture files.

The root `pyproject.toml` mirrors the Python build dependencies used by
EdgeTX's existing `requirements.txt`. Running inside the Nix dev shell gives
CMake a Python with Pillow, clang bindings, lz4, jinja2, and the other scripts
dependencies.

The harness writes simulator builds under `build/ui-harness` by default. Set
`EDGETX_UI_BUILD_ROOT=/tmp/edgetx-ui-build` to use a separate build root.

## Target

Supported targets:

- `tx16s`, configured as `PCB=X10` and `PCBREV=TX16S`
- `tx16smk3`, configured as `PCB=TX16SMK3`

Default fixtures are created on demand:

```text
tools/ui-harness/fixtures/sdcard-tx16s
tools/ui-harness/fixtures/settings-tx16s
```
