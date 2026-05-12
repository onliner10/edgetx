---
# battery-guard-fix-preselection
title: Auto-confirm exactly one voltage-compatible pack among multiple compatible packs
status: done
type: bug
priority: high
parent: battery-guard
created_at: 2026-05-12T12:25:21Z
updated_at: 2026-05-12T12:25:21Z
---

Fix preselection so a model with multiple compatible packs can still auto-confirm when the observed voltage matches exactly one configured pack slot. Candidate count and match count are different; only match count should control auto-confirm vs prompt.

Acceptance criteria: 3S and 4S compatible packs at a 3S voltage auto-confirm the 3S pack, while two voltage-compatible packs require confirmation.
