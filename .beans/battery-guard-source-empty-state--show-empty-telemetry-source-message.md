---
# battery-guard-source-empty-state
title: Show explicit empty state for battery telemetry source pickers
status: done
type: task
priority: normal
parent: battery-guard
created_at: 2026-05-12T11:07:37Z
updated_at: 2026-05-12T11:07:37Z
---

Fix the battery monitor source picker empty state so users do not see a blank dialog when no matching telemetry sensor exists. Current relevant files are `radio/src/gui/colorlcd/model/model_setup.cpp`, `radio/src/gui/colorlcd/controls/sourcechoice.cpp`, and `radio/src/gui/colorlcd/libui/choice.cpp`.

For the battery Voltage source picker, show explicit empty text such as `No matching telemetry voltage sensor found` when there are no voltage sensors. For the Capacity source picker, show `No matching telemetry capacity sensor found` when there are no mAh sensors. Keep `MIXSRC_NONE` selectable.

Prefer a local solution for the battery source pickers if generic `SourceChoice` changes would affect unrelated UI. If a generic `Choice` or `SourceChoice` empty-state hook is added, verify all existing menus still behave the same when they have available values.

Acceptance criteria: opening the battery Voltage source picker with no voltage telemetry sensors shows an explicit message instead of an empty menu, opening the Capacity source picker with no mAh telemetry sensors shows an explicit message, and source selection still works when matching sensors exist.

Verification: add UI MCP coverage for the empty source selector state. Verify both UI tree text and screenshot. Run `tools/check-ui-escape-hatches.py` if generic UI code changes, then `git diff --check`.
