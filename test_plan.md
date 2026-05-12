# Edge16 Battery Guard UI Harness QA Test Plan

## Spec Under Test

- Battery guard supports LiPo pack confirmation using configured voltage telemetry.
- Capacity telemetry is primary for alerts when usable; voltage is backup/emergency.
- Arming is blocked only when transitioning disarmed to armed with unconfirmed or invalid battery state.
- Already armed model must not be force-disarmed by telemetry loss or mismatch.
- UI must make setup, compatible packs, empty source states, prompts, and status understandable.
- Stored radio/model pack configuration must persist across navigation and restart.

## Primary Target

- `tx16s` with `tools/ui-harness/fixtures/settings-tx16s` and SD fixture.
- Focused `tx16smk3` smoke only if target-specific simulator confirmation is needed; most behavior is shared and already build-tested.

## Test Cases

1. Startup And Baseline Stability

- Start `tx16s` simulator with battery QA fixture.
- Skip storage warning if present.
- Verify home screen loads, no crash/assert/repeated warning in logs.
- Screenshot: `startup-home`.
- Expected: normal home screen, no battery prompt before telemetry/state requires it.

2. Model Battery Page Navigation

- Navigate to Model Setup, open `BATTERY`.
- Verify Model Battery page is reachable and readable.
- Verify status text is visible and live.
- Verify source fields show configured `VFAS` and `Capa` where fixture expects them.
- Screenshot: `model-battery-page`.
- Expected: no stuck selector, no blank/ambiguous status, page layout fits TX16S screen.

3. Source Empty-State UX

- Open voltage source picker in a state where no matching voltage source is available if fixture/tooling allows.
- Verify empty text says `No voltage telemetry sensor`.
- Open capacity source picker similarly.
- Verify empty text says `No capacity telemetry sensor`.
- Screenshot: `source-empty-state`.
- Expected: unit-filtered empty messages, not a generic empty list or wrong-unit sensor.

4. Global Battery Packs Page

- Navigate to Radio Setup battery pack library.
- Verify page is reachable.
- Verify pack rows show cell/capacity labels like `3S 2200mAh`.
- Edit cells/capacity boundary values where UI permits.
- Screenshot: `battery-packs-page`.
- Expected: edits are predictable, no slot compaction, labels remain understandable.

5. Single Compatible Pack Auto-Confirm

- Enable telemetry streaming.
- Set `VFAS=1200` for 12.00V 3S.
- Wait through presence debounce.
- Verify status becomes confirmed without a prompt when only one configured compatible pack matches.
- Screenshot: `auto-confirm-3s`.
- Expected: confirmed pack shown, no unnecessary dialog.

6. Multi-Pack Confirmation Prompt

- Configure or use fixture state with two compatible same-cell candidates if available.
- Set voltage matching both candidates.
- Verify confirmation dialog appears with pack choices.
- Cancel/back from dialog.
- Verify state remains not confirmed and arming remains blocked.
- Screenshot: `multi-pack-prompt`, `prompt-cancelled`.
- Expected: prompt is clear, cancel is safe, no accidental confirmation.

7. Confirmation Validation Against Current Voltage

- Trigger prompt with matching voltage.
- Change `VFAS` before confirming so selected pack no longer matches.
- Attempt confirmation.
- Verify confirmation fails closed or status remains mismatch/unconfirmed.
- Screenshot: `stale-confirmation-blocked`.
- Expected: no stale-voltage confirmation.

8. Arming Block Before Confirmation

- Ensure battery monitor is enabled and runtime is unconfirmed/needs confirmation.
- Set `SF` disarmed state, then pulse `SH` using `edgetx_switch_sequence`.
- Verify model does not arm.
- Check audio/log for battery-related block if exposed.
- Screenshot: `arming-blocked`.
- Expected: disarmed to armed transition blocked, UI/audio feedback is understandable.

9. Arming Allowed After Confirmation

- Confirm a valid pack or use auto-confirm.
- Pulse `SH` with `SF` armed position.
- Verify model arms.
- Then set `SF` disarm and verify disarmed state.
- Screenshot: `arming-allowed`, `disarmed`.
- Expected: confirmed pack permits arming; disarm remains normal.

10. Already Armed Telemetry Loss Does Not Force Disarm

- Confirm pack and arm model.
- Disable telemetry streaming or set stale/missing `VFAS`.
- Verify model remains armed until `SF` is moved to disarm.
- Screenshot: `armed-telemetry-loss`.
- Expected: no forced disarm from telemetry loss.

11. Voltage Mismatch Blocks Next Arming

- Confirm pack, disarm, then change `VFAS` to incompatible voltage.
- Attempt to arm.
- Verify arming is blocked and status indicates mismatch.
- Screenshot: `voltage-mismatch-block`.
- Expected: mismatch blocks only next arming attempt, not previously armed state.

12. Capacity Alert Policy

- Confirm pack with capacity source configured.
- Set `Capa` to consumed amount leaving 35%, then 30%, 25%, 20%.
- Check `edgetx_audio_history`.
- Screenshot: `capacity-alert-state`.
- Expected: voice callouts use remaining percent values, no duplicate spam.

13. Voltage Backup Alert Policy

- With capacity telemetry usable, set low `VFAS` near backup threshold, expected around `3.30V/cell`.
- Check `edgetx_audio_history`.
- Expected: voltage backup alert speaks per-cell voltage only at emergency/backup threshold.

14. Persistence Across Page Changes

- Configure pack/model battery values.
- Leave Model Battery page, return.
- Verify values and status remain consistent.
- Screenshot: `persistence-page-return`.
- Expected: no reset, no stale UI text.

15. Persistence Across Simulator Restart

- Stop and restart simulator with same settings fixture/storage.
- Navigate back to Model Battery and Battery Packs.
- Verify global pack library, model compatibility mask, selected pack, source indexes persist.
- Screenshot: `persistence-restart`.
- Expected: persisted settings reload cleanly, no storage warning regression beyond expected startup warning.

16. Log And Crash Sweep

- After all scenarios, call `edgetx_log tail=300`.
- Check for crashes, asserts, repeated warnings, LVGL errors, storage parse failures.
- Capture audio history tail.
- Expected: logs clean apart from known benign startup/debug noise; audio history matches expected alerts only.

## Execution Notes For Build Mode

- Build first: `edgetx_build_simulator target=tx16s`.
- Start with fixtures: `edgetx_start_simulator target=tx16s sdcard=tools/ui-harness/fixtures/sdcard-tx16s settings=tools/ui-harness/fixtures/settings-tx16s`.
- Use `automation_id` first, exact text second.
- Use `edgetx_switch_sequence` for `SH` arming pulses.
- Use `edgetx_set_telemetry_streaming` and `edgetx_set_telemetry` for deterministic sensor values.
- Store screenshots under `build/ui-harness/screenshots/battery-guard-qa-final`.

## Pass Criteria

- No Critical/High defects.
- Arming behavior matches safety invariants.
- Confirmation cannot be stale or accidental.
- Alerts are correct and not spammy.
- UI is understandable and persistent.
- Logs have no crash/assert/error pattern.
- Evidence includes screenshots, log tail, and audio history.

## Risks To Call Out If Not Fully Executable

- Multi-pack prompt may require editing fixture state if current fixture only has one compatible pack.
- Source empty-state may require a temporary model/sensor state without matching telemetry units.
- Restart persistence depends on simulator writing settings to the fixture copy used for the run.
