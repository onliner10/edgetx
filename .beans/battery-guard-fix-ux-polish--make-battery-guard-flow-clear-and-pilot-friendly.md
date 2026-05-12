---
# battery-guard-fix-ux-polish
title: Make battery guard flow clear and pilot-friendly
status: done
type: task
priority: normal
parent: battery-guard
created_at: 2026-05-12T12:25:21Z
updated_at: 2026-05-12T14:32:00Z
---

Polish the battery guard UI after safety fixes. The prompt should show observed voltage, inferred cell-count range, recommended/valid choices only, and clear reasons for setup/mismatch states. Pack rows should show meaningful labels, not only slot numbers.

Acceptance criteria: UI tree and screenshot show clear user-facing text for auto-confirmed, prompt, mismatch, no voltage, and needs setup states.

Progress: fixed stale Model Battery status text by making the status value live; tx16s UI shows `3S 2200 confirmed`, clear no-voltage/no-setup/mismatch status strings exist, compatible pack rows show cell/capacity labels, capacity alerts speak remaining percentage (`35/30/25/20%`), and voltage backup alerts speak per-cell voltage.
