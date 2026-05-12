---
# battery-guard
title: Voltage-driven LiPo battery pack confirmation and arming guard
status: done
type: epic
priority: critical
created_at: 2026-05-12T11:07:37Z
updated_at: 2026-05-12T11:07:37Z
---

Implement battery identity as a safety preflight gate for Edge16 TX16S MK2 and TX16S MK3. The radio must use the configured flight battery voltage source as the exclusive signal for battery presence, battery change, voltage mismatch, and pack preselection. Telemetry link state, RSSI, capacity telemetry, and generic `TELEMETRY_STREAMING()` are not reliable enough for battery change detection, especially when FrSky telemetry drops near the receiver.

V1 supports LiPo packs only. Do not add a chemistry selector for the global battery library. Capacity telemetry is only used after a pack is confirmed, and only to compute consumed capacity relative to the session baseline.

Safety invariants: do not change mixer output generation, pulse timing, ADC sampling, watchdog behavior, or emergency shutdown behavior. The arming guard may block a transition from disarmed to armed, but it must never force-disarm an already armed model. Mixer code must use cached runtime state only: no telemetry locks, no filesystem I/O, no UI allocation, no dynamic allocation, and no sensor scanning in the arming path.

Implementation tasks in intended order: `battery-guard-voltage-policy`, `battery-guard-storage`, `battery-guard-runtime`, `battery-guard-confirmation-baseline`, `battery-guard-arming`, `battery-guard-radio-ui`, `battery-guard-model-ui`, `battery-guard-prompt-ui`, `battery-guard-source-empty-state`, `battery-guard-verification`.
