---
# battery-guard-fix-telemetry-source
title: Correct voltage source encoding, unit validation, and conversion
status: done
type: bug
priority: critical
parent: battery-guard
created_at: 2026-05-12T12:25:21Z
updated_at: 2026-05-12T12:25:21Z
---

Fix battery monitor telemetry source handling. `sourceIndex` and `currentIndex` are 1-based persisted sensor selectors where `0` means none. Runtime and UI must preserve that encoding, reject wrong-unit sensors, and convert telemetry item values from the sensor unit/precision to the requested destination unit/precision.

Acceptance criteria: default source 0 is unavailable, `UNIT_VOLTS` and `UNIT_CELLS` are accepted as voltage sources, non-voltage sensors are rejected for voltage, `UNIT_MAH` is required for capacity, and source picker round-trips preserve the persisted encoding.
