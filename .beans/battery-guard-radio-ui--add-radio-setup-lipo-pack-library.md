---
# battery-guard-radio-ui
title: Add Radio Setup LiPo pack library page
status: done
type: task
priority: normal
parent: battery-guard
created_at: 2026-05-12T11:07:37Z
updated_at: 2026-05-12T11:07:37Z
---

Add the global LiPo pack library UI under Radio Setup. Current likely files are `radio/src/gui/colorlcd/radio/radio_setup.cpp` and related color-LCD setup menu files.

The page must list active global packs from `g_eeGeneral.batteryPacks`. Add and edit fields are `Name`, `Cells`, and `Capacity`. Do not show chemistry/type; v1 is LiPo-only. Delete must clear the `active` flag and leave the slot index stable. Do not compact or reorder slots because model compatibility masks refer to stable slots.

Use existing EdgeTX/Edge16 `Window` and LVGL ownership patterns. Avoid retaining callbacks that can fire after object destruction. UI code may allocate like nearby setup pages, but telemetry and mixer code must remain UI-free.

Acceptance criteria: a user can create, edit, and delete a `3S 2200` LiPo pack; deleted slots become inactive without changing other slots; active pack names are bounded by `LEN_BATTERY_PACK_NAME`; capacity and cell count limits are enforced; storage is marked dirty on changes.

Verification: add a UI MCP flow or simulator test if the harness supports setup-page navigation. At minimum build simulator for `tx16s`, navigate to the page, create a pack, edit it, delete it, and capture UI tree plus screenshot. Run `tools/check-ui-escape-hatches.py` after UI lifetime changes and `git diff --check`.
