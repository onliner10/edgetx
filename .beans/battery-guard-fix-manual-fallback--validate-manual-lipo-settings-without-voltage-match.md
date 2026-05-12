---
# battery-guard-fix-manual-fallback
title: Validate manual LiPo fallback from configured model fields
status: done
type: bug
priority: medium
parent: battery-guard
created_at: 2026-05-12T12:25:21Z
updated_at: 2026-05-12T12:25:21Z
---

Fix manual fallback detection. `flightBatteryPromptAllowsManual()` must validate model battery fields directly (`BATTERY_TYPE_LIPO`, non-zero cells, positive capacity), not call the voltage-match helper with a zero voltage.

Acceptance criteria: valid manual LiPo settings allow manual confirmation, non-LiPo or incomplete settings do not.
