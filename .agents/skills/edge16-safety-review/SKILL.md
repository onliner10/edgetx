---
name: edge16-safety-review
description: Review Edge16 changes as safety-critical RC transmitter firmware, focusing on concurrency, memory/lifetime safety, crash resistance, embedded constraints, and robust Rust-inspired design.
---

# Edge16 Safety Review

## Overview

Use this skill when reviewing Edge16 code changes, PRs, commits, local diffs, or proposed designs.

Edge16 is safety-relevant RC transmitter firmware. A radio crash, hang, missed deadline, corrupted model state, or unsafe module output can lead to loss of aircraft control and possible injury. Treat plausible runtime failures as safety defects, not just software bugs.

Review from the perspective of a Rust engineer working in embedded C/C++:

- Prefer designs that make illegal states unrepresentable.
- Prefer ownership, lifetime, and state-machine structure over comments and scattered invariants.
- Prefer compile-time guarantees, fixed capacities, and explicit error states over dynamic checks where practical.
- Accept broader refactoring when it materially reduces crash, race, aliasing, lifetime, or edge-case risk.
- Do not suggest refactoring for aesthetics alone. Each refactor must reduce a concrete class of defects or simplify a safety-critical invariant.
- Keep embedded limits in mind: CPU, RAM, stack, flash, timing deadlines, ISR constraints, and no-exceptions/no-RTTI firmware style.

## Review Priority

Prioritize findings in this order:

1. Bugs that can crash, hang, corrupt memory, corrupt persisted model/config data, destabilize RF output, or break control-path timing.
2. Concurrency, ISR/task boundary, lifetime, callback, DMA, shared-state, and cancellation defects.
3. Memory safety defects: buffer bounds, stack growth, use-after-free, dangling references, null dereference, invalid object ownership, uninitialized state, overflow, unsafe casts, alignment, and aliasing.
4. State-machine and edge-case defects: impossible states represented by booleans, partial failure cleanup, rollback gaps, missing cancellation, timeout/retry behavior, unplug/replug, storage/media failures, telemetry loss, and simulator/hardware target differences.
5. Embedded resource issues: unbounded allocation, dynamic containers in realtime paths, excessive logging, blocking IO, broad locks, sleeps/polling loops, recursion, unbounded loops, large stack objects, and avoidable CPU work in hot paths.
6. Test, simulator, sanitizer, analyzer, and policy coverage gaps that would let the above defects escape.

Style-only comments are out of scope unless they obscure a safety invariant or make a defect likely.

## Edge16 Boundaries

Assume only these supported targets unless the diff explicitly says otherwise:

- `tx16s`: RadioMaster TX16S MK2
- `tx16smk3`: RadioMaster TX16S MK3

Be especially strict for changes touching:

- `radio/src/mixer.cpp`, `radio/src/mixer_scheduler.cpp`, `radio/src/tasks/mixer_task.*`
- `radio/src/pulses/**`
- `radio/src/telemetry/**`, `radio/src/serial.cpp`, `radio/src/trainer.cpp`, `radio/src/sbus.cpp`
- `radio/src/hal/**`, `radio/src/targets/common/arm/stm32/**`
- `radio/src/targets/horus/**`, `radio/src/targets/tx16smk3/**`
- `radio/src/gui/colorlcd/**` callback/lifetime code
- `radio/src/lua/**` callback/lifetime code
- `radio/src/storage/**`, USB, SD-card, model/config persistence

Do not normalize risk away because a path is "just UI". UI, Lua, storage, USB, and audio must still avoid destabilizing mixer/protocol timing and must have explicit callback/lifetime ownership.

## Workflow

1. Identify what changed.

- Inspect the diff first, then inspect nearby call sites and owners as needed.
- Classify each touched path as realtime/control, hardware/target, UI/lifetime, storage/USB, Lua/audio, tests/tools, or docs.
- Determine whether the change crosses task, ISR, DMA, callback, LVGL, Lua, storage, USB, or hardware boundaries.
- Determine whether behavior can differ between `tx16s` and `tx16smk3`.

2. Build the safety model.

- List the ownership model for any object whose pointer/reference escapes the current scope.
- List the concurrency model for shared state: writer, reader, task/ISR context, locking/atomic/critical-section rules, and lifetime.
- List state-machine states and transitions. Look for paired booleans or nullable pointers that encode illegal intermediate states.
- Identify failure modes and cleanup: allocation failure, missing SD card, parse failure, timeout, cancellation, unplug/replug, target init failure, and partial construction/destruction.

3. Review for Rust-inspired robustness.

- Ask whether the bug class could be eliminated by stronger types, `enum class` states, RAII cleanup, fixed-size buffers, non-null references, explicit ownership, const-correctness, narrower mutability, or scoped capabilities.
- Prefer moving invariants to constructors, type boundaries, or state transitions instead of checking them repeatedly at use sites.
- Prefer bounded queues, fixed capacities, short critical sections, atomics with clear memory ordering, and single-owner handoff over broad locks or sleeps.
- Prefer explicit cancellation and unregister paths for callbacks over relying on object lifetime convention.
- Prefer total handling of enum/state cases. Missing default behavior is a bug if new states can silently skip safety work.

4. Review embedded constraints.

- Flag heap allocation, dynamic STL containers, `std::function`, `std::string`, `new`, `delete`, `malloc`, `free`, `pvPortMalloc`, or `vPortFree` in realtime or control-adjacent paths.
- Flag blocking filesystem IO, USB waits, audio setup/retry loops, logging, sleeps, polling, or mutex waits in realtime/control paths.
- Flag unbounded loops, recursion, large stack allocations, expensive formatting, and repeated work in hot paths.
- Flag arithmetic that can overflow, divide by zero, or perform unsafe modulo/division in control paths.
- Flag any target guard that could accidentally affect unsupported boards or miss `tx16s`/`tx16smk3`.

5. Review tests and proof.

- Require focused tests for the exact bug class when feasible.
- For concurrency/lifetime changes, prefer sanitizer, stress, simulator, or static-analysis coverage over manual reasoning alone.
- For target/HAL/control changes, expect both supported targets to be covered.
- For UI/LVGL changes, expect tree-level verification plus screenshot/framebuffer evidence when behavior is visible.
- For storage/YAML changes, expect malformed input, missing media, partial write, and rollback/error-path coverage where practical.

## Finding Format

Findings are the main output. If there are findings, put them first and order by severity.

Use this structure for each finding:

```text
[Severity] file:line - concise title
Why this is dangerous: concrete crash/race/memory/control/timing failure mode.
Trigger: realistic sequence or edge case that reaches the defect.
Fix direction: smallest robust design change, preferably one that strengthens ownership/state/lifetime at compile time.
Verification: focused test, sanitizer, simulator flow, analyzer, or target build that should catch it.
```

Severity guide:

- Critical: plausible loss of control, radio crash/hang, memory corruption, uncontrolled module output, watchdog/emergency shutdown regression, or corrupted persisted model state that can affect control.
- High: race/lifetime/resource defect with realistic crash or timing risk, but less direct control impact.
- Medium: missing edge-case handling, partial failure cleanup gap, target leakage, or weak invariant likely to become a safety bug.
- Low: test/proof gap, maintainability issue tied to a safety invariant, or non-critical resource inefficiency.

Avoid vague comments like "consider refactoring". Name the exact invariant that should move into a type, owner, state machine, or RAII guard.

## Acceptable Refactoring Advice

Recommend broad refactoring only when it clearly removes or contains a defect class. Examples:

- Replace paired booleans and nullable owner pointers with one `enum class` state plus explicit transition function.
- Replace manual register/unregister callback lifetime with an RAII subscription token or single owner that cancels before destruction.
- Replace shared mutable buffers with single-owner handoff or fixed-capacity message queues.
- Replace scattered partial-init cleanup with a local guard that makes rollback automatic on every early return.
- Split realtime/control logic away from UI/storage side effects so timing-critical code cannot block or allocate.

Do not recommend broad refactoring when a local fix preserves the same safety properties with less risk.

## Commands To Recommend

Do not claim checks passed unless they were actually run. Recommend the narrowest command that proves the specific concern.

Common Edge16 checks:

```bash
git diff --check
nix develop -c python3 tools/check-safe-division.py
nix develop -c python3 tools/check-ui-escape-hatches.py
```

For firmware behavior that can differ by supported target:

```bash
nix develop -c env FLAVOR=tx16s EDGE16_UV_ACTIVE=1 EXTRA_OPTIONS='-DWARNINGS_AS_ERRORS=YES -DEDGE16_SAFETY_CHECKS=ON -DDISABLE_COMPANION=ON' uv run --with-requirements requirements.txt bash ./tools/commit-tests.sh
nix develop -c env FLAVOR=tx16smk3 EDGE16_UV_ACTIVE=1 EXTRA_OPTIONS='-DWARNINGS_AS_ERRORS=YES -DEDGE16_SAFETY_CHECKS=ON -DDISABLE_COMPANION=ON' uv run --with-requirements requirements.txt bash ./tools/commit-tests.sh
```

For memory/lifetime bugs:

```bash
nix develop -c env FLAVOR=tx16s EDGE16_UV_ACTIVE=1 EXTRA_OPTIONS='-DEDGE16_SANITIZERS=address,undefined -DEDGE16_SAFETY_CHECKS=ON -DDISABLE_COMPANION=ON' uv run --with-requirements requirements.txt bash ./tools/commit-tests.sh
```

For race/concurrency bugs:

```bash
nix develop -c env FLAVOR=tx16s EDGE16_UV_ACTIVE=1 EXTRA_OPTIONS='-DEDGE16_SANITIZERS=thread -DEDGE16_SAFETY_CHECKS=ON -DDISABLE_COMPANION=ON' uv run --with-requirements requirements.txt bash ./tools/commit-tests.sh
```

For firmware policy:

```bash
NIXPKGS_ALLOW_UNFREE=1 nix shell --impure nixpkgs#semgrep -c semgrep scan --error --jobs 24 --config p/c --config .semgrep/edge16-firmware.yml --include 'radio/src/**' --exclude 'radio/src/thirdparty/**' --exclude 'radio/src/translations/**' --exclude 'radio/src/tests/**'
```

## Final Review Response

Use this final structure:

```text
Findings
- [Severity] file:line - title
  Why this is dangerous: ...
  Trigger: ...
  Fix direction: ...
  Verification: ...

Open Questions
- Only questions that affect safety assessment.

Residual Risk
- What could not be proven from the diff or unrun checks.

Safety Assessment
- SAFE / NEEDS REVIEW / UNSAFE
```

If no findings are found, say that explicitly, then list residual risks and unrun checks. Do not fill the response with summaries before findings.
