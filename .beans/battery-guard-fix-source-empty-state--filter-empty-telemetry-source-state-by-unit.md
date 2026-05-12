---
# battery-guard-fix-source-empty-state
title: Filter battery source empty states by required telemetry unit
status: done
type: bug
priority: medium
parent: battery-guard
created_at: 2026-05-12T12:25:21Z
updated_at: 2026-05-12T14:32:00Z
---

Fix the Model Battery source picker empty states so they check for matching telemetry units, not just any available sensor. Voltage requires `UNIT_VOLTS` or `UNIT_CELLS`; capacity requires `UNIT_MAH`.

Acceptance criteria: a model with only mAh telemetry shows the no-voltage-sensor message, and a model with only voltage telemetry shows the no-current-sensor message.

Progress: code filters source availability by `UNIT_VOLTS`/`UNIT_CELLS` for voltage and `UNIT_MAH` for capacity. The tx16s fixture now includes discoverable `VFAS` and `Capa` telemetry sensors, positive source selection works, and the UI shows explicit no-voltage/no-capacity sensor messages when matching sensors are absent.
