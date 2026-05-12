---
# battery-guard-fix-battery-number-edit
title: Make battery cells and capacity edits predictable
status: done
type: task
priority: normal
parent: battery-guard
created_at: 2026-05-12T13:13:54Z
updated_at: 2026-05-12T14:32:00Z
---

Investigate and fix unpredictable numeric editing on the Model Battery page, especially `Cells` and `Capacity`, after the selector/focus bugs are fixed or ruled out.

Observed QA evidence: on `Battery 1`, clicking `Cells` value `0`, rotating three steps, and pressing `ENTER` resulted in `12` instead of an expected small increment such as `3`. This may be caused by stale modal/focus state, accelerated encoder behavior, or an incorrect NumberEdit interaction pattern.

Acceptance criteria: `Cells` can be set predictably to `3`, `Capacity` can be set predictably to `2200`, min/max bounds are enforced, focus stays on the intended field while editing, and no unrelated modal or page receives the edit events.

Verification: use tx16s UI harness screenshots/UI tree before, during, and after editing `Cells` and `Capacity`; verify saved values after leaving and re-entering the page; inspect logs; run focused native/UI checks and `git diff --check`.

Result: tx16s UI harness verified global pack `Cells` changed from `6` to `3` with three encoder steps and `Capacity` changed from `0mAh` to `2200mAh` with 22 encoder steps. Model Battery `Cells` changed from `0` to `3` and `Capacity` changed from `0` to `2200`; values persisted after simulator restart via the settings fixture. Focus remained on the intended field, and focused battery/arming native tests passed.
