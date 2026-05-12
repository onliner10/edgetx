---
# battery-guard-fix-runtime-confirmed-slot
title: Store confirmed pack slot for later voltage mismatch detection
status: done
type: bug
priority: critical
parent: battery-guard
created_at: 2026-05-12T12:25:21Z
updated_at: 2026-05-12T12:25:21Z
---

Set `FlightBatteryRuntimeState::confirmedPackSlot` on confirmation and clear it on confirmed absence/reset. Runtime mismatch checks must use the confirmed slot or manual model settings to detect a different cell-count pack while disarmed.

Acceptance criteria: confirming 4S then observing fresh 3S voltage while disarmed enters `VoltageMismatch`; armed models still are not force-disarmed.
