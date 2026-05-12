---
# battery-guard-qa-tx16s-e2e
title: Verify tx16s setup, confirmation, arming, and alerts end-to-end
status: done
type: task
priority: high
parent: battery-guard
created_at: 2026-05-12T13:13:54Z
updated_at: 2026-05-12T16:25:00Z
---

Re-run thorough tx16s MK2 UI harness QA after the storage and UI navigation blockers are fixed. This is the user-facing acceptance pass for the battery guard feature.

Coverage required: create a global `3S 2200` pack, assign it to the active model, enable battery monitoring, configure voltage and capacity telemetry sources, verify single-pack auto-confirm from `VFAS`, verify multi-pack confirmation prompt, verify arming is blocked before confirmation, verify arming succeeds after confirmation, verify voltage alert audio, verify capacity alert audio, verify persistence after page changes and simulator restart, and verify logs remain free of crashes/asserts/repeated warnings.

Acceptance criteria: all required scenarios are executed on tx16s MK2 with screenshots, UI tree assertions, telemetry injection evidence, switch/arming evidence, audio history, and final log review. Any defect found must have exact reproduction steps and severity.

Verification: use the UI harness interactively first, then add or run a repeatable JSON flow if practical. Capture artifacts under `build/ui-harness/screenshots/battery-guard-qa-tx16s`, use `edgetx_set_telemetry_streaming`, `edgetx_set_telemetry`, `edgetx_set_switch`, `edgetx_audio_history`, and `edgetx_log`, and finish with `git diff --check` if any harness flow or fixture is added.

Progress: tx16s UI harness verified setup navigation, Battery Packs editing to `3S 2200mAh`, Model Battery opening, monitor enabled, model `Cells=3`, model `Capacity=2200`, live status refresh, telemetry-driven auto-confirm from `VFAS`, arming block before confirmation, arming after confirmation using deterministic `SF`/`SH` switch sequence, `SF` disarm, capacity voice callout at `35%` remaining, voltage backup callout at `3.3V/cell`, and screenshot evidence under `build/ui-harness/screenshots/battery-guard-qa-tx16s`.
