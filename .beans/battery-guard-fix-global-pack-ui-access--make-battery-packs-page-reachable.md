---
# battery-guard-fix-global-pack-ui-access
title: Make Radio Setup Battery Packs page reachable and usable
status: done
type: task
priority: high
parent: battery-guard
created_at: 2026-05-12T13:13:54Z
updated_at: 2026-05-12T14:32:00Z
---

Ensure the global LiPo pack library UI is reachable from Radio Setup on tx16s MK2 and usable through the UI harness. This is required before model compatibility assignment can be validated end-to-end.

Observed QA evidence: after a clean simulator restart, `SYS`/long-`SYS` testing did not reach a usable Radio Setup `Battery Packs` page during the session. The prior Model Battery modal/focus issue also made global setup validation impossible.

Acceptance criteria: a user can navigate to `Battery Packs`, create a `3S 2200` LiPo pack, edit it, delete it, and verify that deleted slots become inactive without compacting or reordering other slots. The page should expose meaningful visible labels and stable enough UI tree nodes for harness testing.

Verification: on tx16s MK2, capture screenshots/UI tree for the `Battery Packs` list, add flow, edit fields, saved list state, delete state, and restart/reload persistence. Inspect simulator logs and run `tools/check-ui-escape-hatches.py` plus `git diff --check`.

Progress: tx16s UI harness reached Radio Setup `Battery Packs`, opened slot 1, edited it to `3S 2200mAh`, verified the parent list refreshed to show slot `1`, `3S`, and `2200mAh`, and code deletes by clearing the slot active flag without compacting or reordering slots while clearing model compatibility bits for that stable slot.
