---
# battery-guard-model-ui
title: Add Model Battery status, compatibility, and advanced telemetry UI
status: done
type: task
priority: normal
parent: battery-guard
created_at: 2026-05-12T11:07:37Z
updated_at: 2026-05-12T11:07:37Z
---

Redesign the color-LCD Model Setup battery page around flight pack status and compatible LiPo packs. Current relevant file is `radio/src/gui/colorlcd/model/model_setup.cpp`.

Add a flight pack summary row that reflects cached runtime state: `Waiting for voltage`, `No battery observed`, `3S 2200 confirmed`, `Confirmed, voltage unavailable`, `Voltage mismatch`, or `Needs setup`. Add a Compatible packs row that opens a checklist of active global LiPo packs and writes `compatiblePackMask`.

Keep manual/model battery settings as a fallback only when they represent a valid LiPo configuration. Move Voltage source and Capacity source under an Advanced telemetry grouping. Existing non-LiPo monitor settings should show as needs setup for the arming guard instead of being silently accepted.

Do not make the UI infer battery presence from telemetry link state. UI display should read the battery session helper state and should not scan sensors in high-frequency redraw paths beyond what existing setup pages do.

Acceptance criteria: the model page can enable a battery monitor, select compatible global LiPo packs, show accurate runtime status, preserve existing voltage and capacity source configuration, and make non-LiPo configs visibly unsupported for v1.

Verification: add UI MCP tests or manual simulator verification on `tx16s` and `tx16smk3` that marks a pack compatible and observes status text. Run `tools/check-ui-escape-hatches.py` after UI lifetime changes and `git diff --check`.
