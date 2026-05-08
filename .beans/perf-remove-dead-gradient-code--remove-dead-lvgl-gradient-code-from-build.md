---
# perf-remove-dead-gradient-code
title: Remove dead LVGL gradient code from build
status: todo
type: task
priority: low
parent: perf-rendering
created_at: 2026-05-08T10:26:27Z
updated_at: 2026-05-08T10:26:27Z
---

LV_GRAD_CACHE_DEF_SIZE=0 and no gradient API calls exist anywhere in the codebase. draw/sw/lv_draw_sw_gradient.c is compiled into both firmware and simulator but is dead code. Remove it from CMakeListsLVGL.txt to reduce binary size and compile time. Also confirm LV_DITHER_GRADIENT=0 is consistent.
