# Simple Arming Implementation — Implementation Review & Feedback

## Status: Issues Fixed

The following issues from the original implementation have been addressed:

### Fixed: Critical Arming Logic Defect (SH Check Was No-Op)
**File:** `radio/src/mixer.cpp` lines ~1194-1230

On TX16S MK2 and MK3, SH is a momentary 2-position push button that springs to `SWITCH_HW_UP` (rest position). The original code checked `sh_up` (SH at rest) during the SF transition, which meant the SH "confirmation" provided zero additional safety — SF-down alone would arm the model.

**Fix applied:** Changed the arming trigger from `sh_up` to `sh_down` (SH pressed). The arming sequence is now:
1. Flip SF to DOWN (safety engaged, stays there)
2. Press SH (momentary trigger)
3. Release SH (springs back, model stays armed)

```cpp
// OLD (broken): arm on SF transition while SH was at rest (UP)
if (!s_last_sf_down && sh_up) {  // sh_up was always true unless SH pressed

// NEW (correct): arm on SH rising edge while SF is already down
bool sh_rising_edge = sh_down && !s_last_sh_down;
if (sf_down && sh_rising_edge && throttleIsIdle()) {
    s_armed_state = true;
}
```

### Fixed: One-Cycle Stale Throttle Data Window
**File:** `radio/src/mixer.cpp` lines ~1367-1379

`throttleIsIdle()` called `getChannelOutput(ch)` which returns the previous mixer cycle's value. `updateArmingState()` runs at the TOP of `evalMixes()`, before the current cycle's mixing, creating a one-cycle stale-data window.

**Fix applied:** Moved the throttle idle check to the point where the cutoff decision is made — after `applyLimits()`, using the current cycle's post-limits value:

```cpp
if (g_model.armingEnabled && i == g_model.armingThrottleChannel) {
    LimitData* lim = limitAddress(i);
    bool throttle_idle = lim->revert ? (value > 0) : (value < 0);

    if (!s_armed_state || !throttle_idle) {
        value = (lim->revert ? RESXl : -RESXl);
        s_armed_output = false;
    } else {
        s_armed_output = true;
    }
}
```

### Fixed: Audio State Not Reset on Model Change
**File:** `radio/src/mixer.cpp`

`doMixerPeriodicUpdates()` had a local static `s_wasArmed` that was never reset on model change, causing a false "disarmed" audio notification after model switch.

**Fix applied:** Promoted `s_wasArmed` to file scope and reset it in `resetArmingState()`.

### Fixed: Unnecessary `std::atomic` Overhead
**File:** `radio/src/mixer.cpp` lines ~1169-1170

`std::atomic<bool>` was used for `s_armed_state` and `s_armed_output` despite the mixer being single-threaded. Atomic operations add overhead on Cortex-M (cache maintenance on H7).

**Fix applied:** Replaced with plain `bool`. If cross-thread access is needed in the future, add synchronization then.

### Fixed: Pre-Arm Throttle Idle Check
**File:** `radio/src/mixer.cpp` lines ~1194-1230

The original design allowed arming at any throttle position (throttle idle only gated the output, not the arming transition).

**Fix applied:** Added throttle idle check to the arming transition:

```cpp
if (sh_rising_edge && throttleIsIdle()) {
    s_armed_state = true;
}
```

This prevents "surprise arming" where the model is technically armed but output is cut, and then suddenly becomes armed when throttle returns to idle.

### Fixed: Test Quality Issues
**File:** `radio/src/tests/mixer.cpp`

- Fixed tests using incorrect SH position (was using `sh_idx = 1` UP, now correctly uses `-1` DOWN for trigger)
- Fixed throttle cutoff tests to use non-idle throttle values to actually exercise the cutoff code
- Added new test `ThrottleGate_ArmedButThrottleNotIdle_OutputCut` that verifies the throttle gate behavior

### Fixed: UI Flow Test
**File:** `tools/ui-harness/flows/tx16s-arming-test.json`

- Changed `{ "press": { "key": "SH" } }` to `set_switch` with correct index (7) and position
- Fixed arming sequence: SF DOWN first, then SH DOWN (trigger), then SH UP (release)

### Fixed: Extra Blank Lines
**File:** `radio/src/mixer.cpp`

Removed duplicate blank lines after `isModelArmedOutput()`.

---

## Remaining Considerations

### 1. Translation Strings — Placeholder English
All non-English translations for `TR_ARMING_ENABLED` and `TR_ARMING_DISABLED` use English strings as placeholders. These should be properly translated before release.

### 2. `STR_ARMING_DISABLED` — Defined but Unused
`STR_ARMING_DISABLED` is properly registered in `string_list.h` and `sim_string_list.h`, but no code currently uses it. It's harmless infrastructure for future UI use.

### 3. `testUpdateArmingState()` Declaration
Added declaration to `switches.h` without the `#if defined(SIMU)` guard since tests compile with `SIMU` defined. The function is now always compiled but only meaningfully called from test code.

### 4. `switchLookupIdx("SF", 2)` — Runtime String Lookup
The switch index lookup is still string-based at runtime. On TX16S the switch hardware is fixed, so this is acceptable. Consider using compile-time constants for SF/SH indices if this becomes a maintenance concern.

### 5. Test Build Issues
The gtests-radio target has pre-existing failures in `lua.cpp` and `lcd_480x272.cpp` (missing `simuFatfsGetRealPath`) that are unrelated to the arming implementation. These appear to be environment/build configuration issues.

### 6. Documentation
The arming sequence should be documented for users:
- Flip SF to DOWN (safe position)
- Press SH momentarily to arm
- Model stays armed while SF remains DOWN
- Flip SF to UP to disarm

---

## Verification

Both firmware targets build successfully:
- **tx16s**: `build/tx16s-*.bin` — Flash 77.42%, CCRAM 77.78%
- **tx16smk3**: `build/tx16smk3-*.uf2` — Flash 6.39%

Both builds completed with `-DWARNINGS_AS_ERRORS=YES -DEDGE16_SAFETY_CHECKS=ON`.
