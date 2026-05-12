---
# battery-guard-storage
title: Add LiPo pack library storage and model compatibility fields
status: done
type: task
priority: high
parent: battery-guard
created_at: 2026-05-12T11:07:37Z
updated_at: 2026-05-12T11:07:37Z
---

Add persistent storage for a global LiPo pack library and per-model compatible pack selection. Current relevant files are `radio/src/dataconstants.h`, `radio/src/datastructs_private.h`, `radio/src/datastructs.h`, `radio/src/storage/yaml/yaml_datastructs_x10.cpp`, and `radio/src/storage/yaml/yaml_datastructs_tx16smk3.cpp`.

Add `MAX_BATTERY_PACKS = 16` and `LEN_BATTERY_PACK_NAME = 12`. Add `BatteryPackData` with name, capacity, cell count, active bit, and padding only. Do not add `batteryType` to `BatteryPackData`; v1 is LiPo-only.

Add `BatteryPackData batteryPacks[MAX_BATTERY_PACKS]` to `RadioData` near radio-wide user settings such as `globalTimer`. Extend `BatteryMonitorData` with `selectedPackSlot` and `compatiblePackMask`. `selectedPackSlot` uses `0` for the manual/model LiPo snapshot and `1..16` for global pack slots. `compatiblePackMask` uses bit N for global slot N.

YAML updates must be scoped to supported targets only: `yaml_datastructs_x10.cpp` and `yaml_datastructs_tx16smk3.cpp`. Add `struct_BatteryPackData`, add `YAML_ARRAY("batteryPacks", 128, 16, struct_BatteryPackData, battery_pack_is_active)` to `struct_RadioData`, change battery monitor array width from `48` to `72`, and add `selectedPackSlot` plus `compatiblePackMask` after `currentIndex`.

Acceptance criteria: `CHKSIZE(BatteryPackData, 16)` passes, `CHKSIZE(BatteryMonitorData, 9)` passes, supported `RadioData` sizes increase by 256 bytes, supported `ModelData` sizes increase by 12 bytes, old model files default new fields to zero, and no unsupported board YAML files are changed.

Verification: add or update storage/YAML tests for TX16S MK2 and TX16S MK3 if such tests exist. At minimum run a compile or storage-focused native test that exercises `check_struct()`, then `git diff --check`.
