---
# battery-guard-voltage-policy
title: Add LiPo voltage matching helpers for battery guard
status: done
type: task
priority: high
parent: battery-guard
created_at: 2026-05-12T11:07:37Z
updated_at: 2026-05-12T11:07:37Z
---

Add the centralized voltage policy used by the battery session state machine. Current relevant files are `radio/src/telemetry/battery_monitor.h`, `radio/src/telemetry/telemetry.cpp`, and `radio/src/tests/battery_monitor.cpp`.

The policy must be LiPo-only for v1. Add constants equivalent to `FLIGHT_BATTERY_NO_BATTERY_MAX_CV = 100`, `FLIGHT_BATTERY_LIPO_MATCH_MIN_PER_CELL_CV = 300`, `FLIGHT_BATTERY_LIPO_MATCH_MAX_PER_CELL_CV = 435`, `FLIGHT_BATTERY_PRESENT_DEBOUNCE_SECONDS = 2`, and `FLIGHT_BATTERY_NO_BATTERY_DEBOUNCE_SECONDS = 3`. Keep existing low-voltage alert threshold behavior separate from pack matching.

Implement pure helpers that can answer whether a total pack voltage in centivolts is plausible for a LiPo cell count and whether exactly one candidate matches. Use total pack voltage ranges derived from cell count. Do not use `TELEMETRY_STREAMING()`, RSSI, telemetry link status, capacity, or current sensor values in these helpers.

Acceptance criteria: 3S voltage around 11.4 V matches 3S and not 4S, 4S voltage around 15.2 V matches 4S and not 3S, boundary values are tested, and overlapping or ambiguous candidate sets do not auto-confirm. Missing/stale telemetry is not represented as absent by these helpers.

Verification: add native unit tests in `radio/src/tests/battery_monitor.cpp` for LiPo voltage range matching, exact-one candidate selection, ambiguous selection rejection, and invalid cell counts. Run the focused native test target if available, then `git diff --check`.
