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
touch, wait, screenshot, status, live UI-tree, selector click, selector
long-click, visibility assertion, storage-warning skip, and run-flow tools. It
uses the same Python core as the CLI.

For exploratory QA, keep one MCP simulator session running and use the live
tree instead of guessing key paths or coordinates:

```text
edgetx_start_simulator target=tx16s
edgetx_status  # wait until startup_completed is true before screenshot QA
edgetx_skip_storage_warning_if_present
edgetx_ui_tree
edgetx_click automation_id=model.model2.yml
edgetx_ui_tree
edgetx_screenshot name=after-model-click
```

The `ui_tree` result is a snapshot of the running color-LCD LVGL tree. Nodes
include role, visible text, stable `automation_id` where available, bounds,
state, and available actions. Prefer `automation_id` selectors, then exact text
or `text_contains`; use raw coordinates only when the tree lacks a meaningful
node. Selector `click` and `long_click` invoke the selected action on the
menu/UI thread without fixed sleeps; raw `touch` and `drag` remain available
when the touch timing itself is under test. JSON flows are best treated as
replay artifacts after the interactive path is known.

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
