---
# battery-guard-fix-confirmation-validation
title: Revalidate pack selection against current voltage before confirmation
status: done
type: bug
priority: critical
parent: battery-guard
created_at: 2026-05-12T12:25:21Z
updated_at: 2026-05-12T12:25:21Z
---

Fix confirmation so the selected global pack must still match the latest usable configured voltage before `confirmFlightBatteryPack()` accepts it. The prompt must show only currently voltage-compatible candidates, not the full compatible-pack mask.

Acceptance criteria: a 3S voltage cannot confirm a 4S pack, stale/missing voltage fails closed, manual confirmation is only accepted for valid manual LiPo settings, and the dialog cannot confirm choices outside the current prompt candidate mask.
