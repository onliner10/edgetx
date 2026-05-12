---
# battery-guard-confirmation-baseline
title: Apply confirmed pack settings and session capacity baseline
status: done
type: task
priority: high
parent: battery-guard
created_at: 2026-05-12T11:07:37Z
updated_at: 2026-05-12T11:07:37Z
---

Implement the confirmation side effects and consumed-capacity baseline logic. Current relevant files are `radio/src/telemetry/telemetry.cpp`, `radio/src/telemetry/battery_monitor.h`, and `radio/src/tests/battery_monitor.cpp`.

When `confirmFlightBatteryPack()` receives a global pack slot, copy that pack into the model monitor as LiPo type, cell count, capacity, and selected slot. When it receives manual/model slot `0`, require the current model monitor to already be a valid LiPo configuration. If persistent fields change, mark model storage dirty using the existing firmware storage dirty mechanism; do not depend on UI-only macros from telemetry code.

On confirmation, capture the current mAh sensor value as `consumedBaselineMah` if the configured capacity source is fresh and valid. Use zero if no mAh sensor is configured or fresh. Clear capacity alert mask, voltage-low debounce, voltage alert latch, and prompt latch. Do not clear `consumedBaselineMah` merely because the model is disarmed.

Refactor capacity alerts so they run only after a battery is confirmed. Alert math must use `currentMah - consumedBaselineMah`. If current mAh is lower than the baseline, treat current mAh as the consumed amount for the current session.

Acceptance criteria: confirmed global pack updates model battery monitor fields, manual confirmation refuses non-LiPo settings, baseline is captured exactly once per confirmation, disarming does not reset baseline, and capacity alerts are based on consumed delta rather than absolute telemetry counter value.

Verification: add native tests for confirmation side effects, storage dirty behavior if testable, baseline capture, current-less-than-baseline handling, and threshold alerts using delta consumed capacity. Run focused native tests and `git diff --check`.
