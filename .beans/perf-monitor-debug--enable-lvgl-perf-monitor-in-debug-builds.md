---
# perf-monitor-debug
title: Enable LVGL perf monitor in debug builds
status: todo
type: task
priority: normal
parent: perf-rendering
created_at: 2026-05-08T10:26:27Z
updated_at: 2026-05-08T10:26:27Z
---

LV_USE_PERF_MONITOR is gated behind the UI_PERF_MONITOR CMake option which defaults to OFF. Without it, there is no visibility into FPS or CPU usage during development. Enable LV_USE_PERF_MONITOR automatically when building in Debug configuration or when EDGE16_SAFETY_CHECKS is ON, so developers can measure rendering performance without manually toggling CMake flags. This provides a baseline for measuring the impact of all other perf-rendering tasks.
