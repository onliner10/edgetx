---
# battery-guard-fix-storage-compat
title: Load existing radio settings after battery pack schema change
status: done
type: task
priority: high
parent: battery-guard
created_at: 2026-05-12T13:13:54Z
updated_at: 2026-05-12T13:55:00Z
---

Fix radio settings/YAML compatibility after adding `RadioData::batteryPacks`. The tx16s UI harness fixture and existing SD-card radio settings without `batteryPacks` must load cleanly instead of raising `Unable to read valid radio settings`.

Observed QA evidence: starting `tx16s` with `settings=tools/ui-harness/fixtures/settings-tx16s` failed during `YAML radio settings reader`, logged `radio settings: Reading failed`, `Unable to recover radio data`, and displayed the storage warning `Unable to read valid radio settings`.

Acceptance criteria: old `RADIO/radio.yml` files without `batteryPacks` load successfully, new battery pack fields default inactive/zeroed, updated settings save and reload, and `tools/ui-harness/fixtures/settings-tx16s` boots without a storage warning.

Verification: start `edgetx_start_simulator target=tx16s sdcard=/mnt/c/a/edgetx-bak settings=tools/ui-harness/fixtures/settings-tx16s`, confirm `startup_completed: true`, inspect logs for absence of radio settings read failures, and run storage/YAML focused native tests plus `git diff --check`.

Resolution: added an active-slot YAML predicate for `batteryPacks` and verified tx16s simulator startup with `settings-tx16s` no longer shows the storage warning.
