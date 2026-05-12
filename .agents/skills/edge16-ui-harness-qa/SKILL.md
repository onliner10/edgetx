---
name: edge16-ui-harness-qa
description: Test implemented Edge16 UI or simulator-visible behavior as a QA engineer using the UI harness, with exploratory coverage, edge cases, screenshots, logs, and clear defect reports.
---

# Edge16 UI Harness QA

## Overview

Use this skill when validating an implemented feature, bug fix, or UI-facing behavior in the Edge16 simulator with the UI harness.

Act as a QA engineer, not a demo script writer. Your goal is to ensure the experience is delightful, edge cases are covered nicely, there are no errors or crashes, and behavior matches the intended spec.

Infer the spec from available context before asking the user:

- User request, issue text, acceptance criteria, and prior conversation.
- Git diff, commit, branch, changed files, tests, docs, model/settings fixtures, and UI copy.
- Existing behavior in nearby screens and existing harness flows.
- Edge16 product constraints: supported targets are `tx16s` and `tx16smk3` only.

Ask the user a short question only when expected behavior cannot be inferred well enough to start meaningful testing. If part of the spec is unknown but the rest is testable, test the known behavior and list the assumption.

## QA Mindset

Validate more than the happy path:

- Does the primary flow work exactly as the feature spec implies?
- Is the UI understandable, responsive, and pleasant to use on the TX16S screen?
- Are labels, button text, warnings, empty states, and error messages clear?
- Are disabled, boundary, missing-data, cancellation, back/escape, and repeated-action cases handled cleanly?
- Does state persist or reset as the feature spec requires after page changes, model reload, or simulator restart?
- Are audio, telemetry, switches, dialogs, and storage failures covered when relevant?
- Does the simulator log stay free of crashes, asserts, obvious errors, and repeated warnings?

Do not change the product behavior to make a test pass unless the user explicitly asks for fixes. When you find a defect, capture the smallest reproduction and evidence.

## Simulator Setup

Prefer the MCP UI harness tools when available.

Typical MCP sequence:

```text
edgetx_build_simulator target=tx16s
edgetx_start_simulator target=tx16s
edgetx_status  # wait until startup_completed is true
edgetx_skip_storage_warning_if_present
edgetx_ui_tree
```

Use one persistent simulator session for exploratory QA. Stop and restart only when the test requires boot-time, persistence, fixture, or crash-recovery validation.

CLI fallback when MCP is unavailable:

```bash
nix develop -c tools/ui-harness/edgetx-ui build tx16s
nix develop -c tools/ui-harness/edgetx-ui run-flow tools/ui-harness/flows/tx16s-smoke.json
nix develop -c tools/ui-harness/edgetx-mcp
```

If behavior can differ by supported target, repeat the focused scenario on `tx16smk3`:

```text
edgetx_build_simulator target=tx16smk3
edgetx_start_simulator target=tx16smk3
```

Use custom fixtures only when needed:

```text
edgetx_start_simulator target=tx16s sdcard=tools/ui-harness/fixtures/sdcard-tx16s settings=tools/ui-harness/fixtures/settings-tx16s
```

## Interaction Rules

Drive the UI through observable state, not timing guesses.

- Inspect `edgetx_ui_tree` before choosing an action.
- Prefer selectors in this order: `automation_id`, exact `text`, exact `id`, `text_contains` with `role`, then raw coordinates only when the tree has no meaningful node.
- Use `edgetx_click` and `edgetx_long_click` for visible UI nodes because they run on the menu/UI thread without fixed sleeps.
- Use `edgetx_press`, `edgetx_rotate`, `edgetx_touch`, or `edgetx_drag` when testing physical-key, rotary, touch, or gesture behavior.
- Use short `edgetx_wait` calls only for real asynchronous work, animations, startup, or debounced hardware events.
- After each important action, re-read `edgetx_ui_tree` and verify the expected node, text, state, focus, enabled/checked value, or page transition.
- Capture screenshots for visible before/after states with `edgetx_screenshot`.
- Check `edgetx_log` after risky actions or at the end of the run for errors, asserts, crashes, or suspicious repeated warnings.

Useful specialized tools:

```text
edgetx_assert_visible text="..."
edgetx_set_switch index=5 position=-1  # SF down/back
edgetx_set_telemetry_streaming enabled=true
edgetx_set_telemetry name=VFAS value=1200  # VFAS raw value is volts * 100
edgetx_audio_history tail=200
```

Switch indices are zero-based: `SA=0`, `SB=1`, `SC=2`, `SD=3`, `SE=4`, `SF=5`, `SG=6`, `SH=7`. Switch positions are `-1` down/back, `0` middle, and `+1` up/forward.

## Test Design Workflow

1. Reconstruct the spec.

- Read the change context and write down the expected behavior in your own words.
- Identify the user-visible surfaces: screens, dialogs, navigation paths, telemetry values, switches, audio, persistence, and failure states.
- Identify what must not regress: existing navigation, model selection, home screen stability, storage warnings, and control-adjacent behavior.

2. Build a focused QA matrix.

- Happy path for the implemented feature.
- One or more edge cases from boundaries, missing data, wrong order of actions, repeated actions, cancel/back/escape, disabled states, invalid input, or unavailable telemetry/storage.
- Persistence or restart checks when the feature stores model/radio state.
- Visual and UX checks for copy, layout, button affordance, and whether the next action is obvious.
- Error/crash checks through simulator logs and continued interaction after the scenario.

3. Execute interactively first.

- Start from a known state with default fixtures unless the feature requires a custom setup.
- Use `edgetx_ui_tree` to discover stable selectors and current state.
- Follow the matrix, collecting screenshots and logs as evidence.
- Do not rely on a JSON flow until you have proven the path interactively.

4. Create or run a JSON flow only when useful.

- Use existing flows under `tools/ui-harness/flows/` for smoke or regression coverage.
- Add a new flow only if the scenario should be repeatable and the user asked for or would benefit from a committed regression artifact.
- Flow steps can include `wait`, `ui_tree`, `click`, `long_click`, `assert_visible`, `skip_storage_warning_if_present`, `press`, `long_press`, `rotate`, `touch`, `drag`, and `screenshot`.

Example flow shape:

```json
{
  "target": "tx16s",
  "output": "build/ui-harness/screenshots/example-feature",
  "steps": [
    { "wait": { "ms": 2500 } },
    { "skip_storage_warning_if_present": true },
    { "ui_tree": true },
    { "click": { "automation_id": "example.action" } },
    { "assert_visible": { "text_contains": "Expected", "role": "label" } },
    { "screenshot": "expected-state" }
  ]
}
```

Run it with:

```text
edgetx_run_flow flow_path=tools/ui-harness/flows/example-feature.json
```

## Evidence And Reporting

Report results like QA findings, not a generic summary.

For each defect, include:

- Severity: `Critical`, `High`, `Medium`, or `Low`.
- Scenario and exact steps to reproduce.
- Expected result inferred from the spec.
- Actual result, including UI text, screenshot path, log excerpt, or audio event when relevant.
- Impact on UX, correctness, crash resistance, persistence, or safety.
- The smallest suggested fix direction if obvious.

If no defects are found, state that clearly and list what was covered and what remains untested.

Final response format:

```text
QA Result
- PASS / PASS WITH RISKS / FAIL

Coverage
- Scenarios tested, targets, fixtures, and key edge cases.

Findings
- Defects first, ordered by severity. Say "No findings" if none.

Evidence
- Screenshot paths, logs checked, audio history checked, flow paths, and commands/tools used.

Open Questions
- Only questions that affect whether behavior is correct.
```

Never claim a target, flow, log check, audio check, screenshot, or edge case was tested unless it was actually performed.
