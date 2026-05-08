---
# perf-rendering
title: Optimize LVGL rendering pipeline for firmware performance
status: todo
type: epic
priority: high
created_at: 2026-05-08T10:26:27Z
updated_at: 2026-05-08T10:26:27Z
---

The Edge16 UI rendering pipeline has several low-hanging fruit optimizations. The codebase uses zero LVGL gradients, zero shadows, and zero layer compositing — so those are not bottlenecks. The real costs are: color editor gradient redrawn via 450+ lv_draw_line calls per frame, fonts LZ4-decompressed on first user interaction, line/arc-heavy widgets (spectrum analyzer, curve editor) rendered entirely in software, and no perf monitor in debug builds. This epic tracks 5 tasks to address these.
