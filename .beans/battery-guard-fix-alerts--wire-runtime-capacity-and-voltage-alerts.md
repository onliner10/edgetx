---
# battery-guard-fix-alerts
title: Wire runtime capacity and voltage alerts after confirmation
status: done
type: bug
priority: high
parent: battery-guard
created_at: 2026-05-12T12:25:21Z
updated_at: 2026-05-12T12:25:21Z
---

Integrate the new runtime alert state with the telemetry alarm tick. Capacity alerts must use consumed delta from confirmation baseline, and voltage alerts must use the confirmed LiPo cell count. Alerts must not run before confirmation.

Acceptance criteria: threshold alerts fire once after confirmation, do not fire before confirmation, and missing capacity telemetry is ignored gracefully.
