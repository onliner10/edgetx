---
# battery-guard-runtime
title: Implement voltage-driven flight battery session state
status: done
type: task
priority: high
parent: battery-guard
created_at: 2026-05-12T11:07:37Z
updated_at: 2026-05-12T11:07:37Z
---

Replace the current alert-only flight battery runtime state with a voltage-driven session state machine. Current relevant files are `radio/src/telemetry/telemetry.cpp` and `radio/src/telemetry/battery_monitor.h`.

Add public helpers: `resetFlightBatteryRuntimeState()`, `updateFlightBatterySessions()`, `flightBatteryArmingAllowed()`, `flightBatterySessionState(uint8_t monitor)`, `flightBatteryNeedsPrompt(uint8_t* monitor)`, `flightBatteryPromptPackMask(uint8_t monitor)`, `flightBatteryPromptAllowsManual(uint8_t monitor)`, and `confirmFlightBatteryPack(uint8_t monitor, uint8_t selectedPackSlot)`.

Use states equivalent to `Unknown`, `WaitingForVoltage`, `NoBatteryObserved`, `NeedsConfirmation`, `Confirmed`, `ConfirmedWaitingForVoltage`, `VoltageMismatch`, and `NeedsConfiguration`. Missing, stale, unconfigured, or wrong-unit voltage is `WaitingForVoltage`, not `NoBatteryObserved`. Fresh configured voltage below the no-battery threshold is the only absent/no-battery signal.

Call `resetFlightBatteryRuntimeState()` from `telemetryReset()`. Call `updateFlightBatterySessions()` once per telemetry alarm tick before `checkFlightBatteryAlerts()`. Do not make the session update dependent on `TELEMETRY_STREAMING()` or `g_model.disableTelemetryWarning`.

While disarmed, fresh low voltage after debounce clears confirmation and alert latches. While disarmed, missing or stale voltage blocks a new arming attempt but must not clear an existing confirmation or consumed-capacity baseline. While armed, never demote confirmation, never prompt, and never force-disarm from voltage loss, telemetry loss, or voltage mismatch.

Acceptance criteria: exactly one voltage-compatible candidate auto-confirms, multiple voltage-compatible candidates enter `NeedsConfirmation`, no compatible candidate enters `VoltageMismatch` or `NeedsConfiguration` as appropriate, and the arming helper reads only cached state.

Verification: add native tests for state transitions using controllable voltage sensor freshness and voltage values. Include tests for telemetry loss while disarmed and while armed. Run focused native tests and `git diff --check`.
