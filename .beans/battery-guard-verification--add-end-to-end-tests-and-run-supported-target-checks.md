---
# battery-guard-verification
title: Add end-to-end tests and run supported target safety checks
status: done
type: task
priority: high
parent: battery-guard
created_at: 2026-05-12T11:07:37Z
updated_at: 2026-05-12T23:59:00Z
---

Add final coverage and run the required checks for the complete battery guard epic. This task should happen after the runtime, arming, storage, and UI tasks are implemented.

Native battery session tests must cover fresh voltage transition to confirmation or prompt, one compatible pack auto-confirming, multiple compatible packs requiring confirmation, 3S/4S voltage preselection, confirmation side effects, disarmed unplug clearing confirmation, stale voltage while disarmed preserving baseline but blocking arming, stale telemetry while armed not clearing confirmation, and capacity alerts using consumed delta.

Mixer arming tests must cover no enabled battery monitor preserving existing behavior, unknown pack blocking arming, voltage unavailable blocking arming, voltage mismatch blocking arming, confirmed pack allowing arming, and already armed model staying armed through telemetry loss until SF up.

Storage tests must cover global pack library persistence, model compatibility mask persistence, selected pack slot persistence, old model defaulting new fields to zero, and both supported target YAML layouts.

UI MCP tests must cover creating `3S 2200`, marking it compatible, auto-confirm at unambiguous 3S voltage, prompt at ambiguous same-cell candidates, arming block messages, and explicit source picker empty state.

Required checks: run focused native tests first, then `git diff --check`. For firmware-affecting completion, run `tools/commit-tests.sh` for `FLAVOR=tx16s` and `FLAVOR=tx16smk3`, strict firmware builds for both supported targets, `nix develop -c python3 tools/check-safe-division.py`, `nix develop -c python3 tools/check-ui-escape-hatches.py`, and the firmware semgrep policy command from `AGENTS.md`. If any required check cannot run, document the exact command and why it was skipped.

Acceptance criteria: all new behavior is covered by automated tests where practical, both supported targets remain covered, no unsupported target scope is expanded, and final review concludes `SAFE` or explicitly lists remaining `NEEDS REVIEW` risks.

Progress: focused native battery/runtime/arming/mixer tests passed; tx16s UI harness QA passed earlier with setup, confirmation, arming, alerts, persistence, and screenshot evidence; `tools/commit-tests.sh` passed for both `tx16s` and `tx16smk3`; strict firmware builds passed for both supported targets; `check-safe-division.py`, `check-ui-escape-hatches.py`, Python harness syntax checks, firmware Semgrep policy, and `git diff --check` completed. `git diff --check` emitted only CRLF conversion warnings for tx16s fixture YAML files.
