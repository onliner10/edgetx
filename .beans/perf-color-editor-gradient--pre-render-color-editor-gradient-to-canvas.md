---
# perf-color-editor-gradient
title: Pre-render color editor gradient to canvas
status: todo
type: task
priority: normal
parent: perf-rendering
created_at: 2026-05-08T10:26:27Z
updated_at: 2026-05-08T10:26:27Z
---

color_editor.cpp draws ~450 individual lv_draw_line() calls per frame (3 bars x ~150 rows each) to render the color picker gradient. Each call also computes screenToValue() and getRGB() with HSV/RGB math. This is the single most expensive per-frame operation when the color editor is visible. Pre-render the gradient to an 8-bit alpha canvas or LV_IMG_CF_TRUE_COLOR_ALPHA bitmap once on creation, then blit via DMA2D hardware blend. Only redraw when the color range or bar dimensions change.
