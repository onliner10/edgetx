---
# battery-guard-arming
title: Block arming on unconfirmed or unavailable flight battery session
status: done
type: task
priority: critical
parent: battery-guard
created_at: 2026-05-12T11:07:37Z
updated_at: 2026-05-12T11:07:37Z
---

Integrate the battery session guard into the arming state transition. Current relevant files are `radio/src/mixer.cpp`, the arming state header used by UI/tests, and `radio/src/tests/mixer.cpp`.

Add an arming block reason enum with at least `None`, `BatteryUnknown`, `BatteryVoltageUnavailable`, `BatteryVoltageMismatch`, and `BatteryNeedsConfiguration`. Add a one-shot consumer such as `consumeArmingBlockReason()` for UI/audio feedback.

In `mixer.cpp::updateArmingState()`, keep the current SF/SH sequence. On the SH rising edge, before setting `s_armed_state = true`, call `flightBatteryArmingAllowed()`. If it returns false, leave `s_armed_state = false`, preserve the existing sequence state as appropriate, and record the block reason. If the model is already armed, do not call the battery gate again. SF up remains the only normal disarm path.

The arming path must be bounded and non-blocking. It must not lock telemetry, scan sensors, allocate memory, open UI, play audio directly, touch storage, or inspect raw telemetry items. It must use cached runtime state from the telemetry battery session update.

Acceptance criteria: models with no enabled battery monitor keep existing arming behavior, unknown battery blocks arming, voltage unavailable blocks arming, voltage mismatch blocks arming, confirmed fresh matching voltage permits arming, and a confirmed model that loses telemetry while already armed remains armed until SF up.

Verification: extend `radio/src/tests/mixer.cpp` arming tests for all acceptance cases. Run mixer tests, focused native tests, and `git diff --check`.
