---
# battery-guard-prompt-ui
title: Add voltage-compatible battery confirmation dialog
status: done
type: task
priority: high
parent: battery-guard
created_at: 2026-05-12T11:07:37Z
updated_at: 2026-05-12T11:07:37Z
---

Add the color-LCD confirmation flow for ambiguous voltage-compatible packs. Likely integration points are a UI `checkEvents()` path, `MainWindow::runUiTick()`, or a small helper called by main-window children. Do not allocate or open UI from telemetry or mixer code.

When `flightBatteryNeedsPrompt()` reports a monitor, open a `Battery connected` dialog. Show only packs whose LiPo cell count matches the current fresh voltage according to the central voltage policy. Include a manual/model fallback only when no compatible library pack exists and the current model settings are valid LiPo.

Auto-confirm must happen in the runtime state machine when exactly one candidate matches voltage, so the dialog is only for ambiguous cases. When the user presses a button, revalidate that the selected pack still matches the current fresh voltage before calling `confirmFlightBatteryPack()`. If voltage is now unavailable or mismatched, fail closed and leave the monitor unconfirmed.

The dialog must not appear while armed. It must not repeat every UI tick; use a prompt latch in runtime state and clear it on confirmation, no-battery observation, or setup changes.

Acceptance criteria: one compatible 3S pack at 3S voltage auto-confirms with no dialog, two compatible same-cell packs at matching voltage show the dialog, choosing a pack confirms and captures baseline, stale dialog choices fail closed, and no prompt appears while armed.

Verification: add UI MCP tests with telemetry voltage injection if available. Verify prompt text and button labels through `edgetx_ui_tree`, confirm a choice, and capture a screenshot. Run `tools/check-ui-escape-hatches.py` and `git diff --check`.
