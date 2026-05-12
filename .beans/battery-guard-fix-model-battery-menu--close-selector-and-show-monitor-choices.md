---
# battery-guard-fix-model-battery-menu
title: Close Model Battery selector and show monitor choices
status: done
type: task
priority: high
parent: battery-guard
created_at: 2026-05-12T13:13:54Z
updated_at: 2026-05-12T13:55:00Z
---

Fix the Model Setup `BATTERY` selector so it visibly lists `Battery 1` through `Battery 4`, opens the selected `BatteryMonitorPage`, and removes the selector cleanly when a monitor is selected or canceled.

Observed QA evidence: clicking Model Setup `BATTERY` opened a mostly empty-looking modal. Pressing `ENTER` opened `Battery 1`, but the old `BATTERY` modal remained in the UI tree. After editing fields, visible underlying buttons lost usable click actions and `EXIT`/`SYS` navigation became inconsistent.

Likely root cause to verify: `new Menu(true)` creates a multi-select menu, so selecting a monitor does not auto-close before opening `BatteryMonitorPage`. A single-select menu or explicit close-before-open behavior should be used.

Acceptance criteria: UI tree shows selectable monitor rows, selecting `Battery 1` removes the selector and opens only `BatteryMonitorPage`, `Back`/`EXIT` returns cleanly to Model Setup/home, no stale modal remains, and repeated open/cancel/open cycles work.

Verification: use the tx16s UI harness to navigate `MODEL` -> `BATTERY` -> `Battery 1`, capture before/after screenshots and UI tree, press `EXIT` back to Model Setup and home, inspect logs for crashes/asserts, run `tools/check-ui-escape-hatches.py`, and run `git diff --check`.

Resolution: changed the selector to a single-action menu. UI harness menu rows are LVGL table cells and not exposed as text nodes, but pressing `ENTER` on the first row closes the selector and opens `Battery 1` without a stale modal.
