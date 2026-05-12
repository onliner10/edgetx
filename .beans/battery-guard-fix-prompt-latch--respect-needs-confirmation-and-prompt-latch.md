---
# battery-guard-fix-prompt-latch
title: Respect prompt latch and only prompt for NeedsConfirmation
status: done
type: bug
priority: high
parent: battery-guard
created_at: 2026-05-12T12:25:21Z
updated_at: 2026-05-12T12:25:21Z
---

Fix prompt logic so the UI opens a battery selection dialog only for `NeedsConfirmation`, and only when `promptShown` is false. `NeedsConfiguration` and `VoltageMismatch` must use status/arming feedback, not the selection dialog.

Acceptance criteria: dismissing a prompt does not reopen it every tick, voltage mismatch does not show a pack selector, and reconnect/no-battery/setup changes clear the latch where appropriate.
