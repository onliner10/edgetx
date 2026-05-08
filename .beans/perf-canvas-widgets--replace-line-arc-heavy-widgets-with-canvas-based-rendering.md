---
# perf-canvas-widgets
title: Replace line/arc-heavy widgets with canvas-based rendering
status: todo
type: task
priority: normal
parent: perf-rendering
created_at: 2026-05-08T10:26:27Z
updated_at: 2026-05-08T10:26:27Z
---

The spectrum analyzer (radio_spectrum_analyser.cpp), curve editor (controls/curve.cpp), statistics graphs (view_statistics.cpp), and trim sliders (trims.cpp) each create dozens of lv_line and lv_arc objects. These render entirely in software via lv_draw_line/lv_draw_arc. Replace with canvas-based approach: draw the static background elements (grid lines, axes, labels) once to an LV_IMG_CF_ALPHA_8BIT canvas, then overlay dynamic elements (data lines, cursors) as separate canvas layers. DMA2D can then blit the pre-rendered canvas instead of recomputing every line/arc per frame.
