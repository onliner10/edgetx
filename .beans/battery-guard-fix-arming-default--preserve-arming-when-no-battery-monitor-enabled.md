---
# battery-guard-fix-arming-default
title: Preserve arming behavior when no battery monitor is enabled
status: done
type: bug
priority: critical
parent: battery-guard
created_at: 2026-05-12T12:25:21Z
updated_at: 2026-05-12T12:25:21Z
---

Fix the battery arming gate so existing models without any enabled battery monitor keep the original SF/SH arming behavior. `flightBatteryArmingAllowed()` must return true when no battery monitors are enabled, and false only when at least one enabled monitor is not confirmed.

Acceptance criteria: existing arming tests pass, `NoEnabledMonitor_ArmsWithoutBatteryGate` passes, and the mixer arming path remains bounded and cached-state-only.
