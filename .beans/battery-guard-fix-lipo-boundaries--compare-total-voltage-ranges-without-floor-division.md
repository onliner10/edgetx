---
# battery-guard-fix-lipo-boundaries
title: Compare LiPo total voltage ranges without floor division
status: done
type: bug
priority: high
parent: battery-guard
created_at: 2026-05-12T12:25:21Z
updated_at: 2026-05-12T12:25:21Z
---

Fix `flightBatteryPackMatchesLipo()` so it compares total pack voltage against `minPerCell * cellCount` and `maxPerCell * cellCount`. Do not divide and floor per-cell voltage.

Acceptance criteria: 1305 cV matches 3S, 1306 cV does not, and other boundary tests pass.
