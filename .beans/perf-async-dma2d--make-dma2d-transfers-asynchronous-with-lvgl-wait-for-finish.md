---
# perf-async-dma2d
title: Make DMA2D transfers asynchronous with LVGL wait_for_finish
status: todo
type: task
priority: high
parent: perf-rendering
created_at: 2026-05-08T10:26:27Z
updated_at: 2026-05-08T10:26:27Z
---

The STM32 DMA2D driver is fully synchronous — every blend call busy-waits on DMA2D->CR & DMA2D_CR_START. Worse, _lv_gpu_stm32_dma2d_start_dma_transfer() has a FIXME comment at line 631: it calls await_dma_transfer_finish() immediately after starting DMA2D because deferring the wait corrupts the mask buffer. The driver also does not register a wait_for_finish callback, unlike SWM341/ARM2D/PXP/VGLite backends. To make it async: (1) register lv_draw_stm32_dma2d_wait_for_finish in draw_ctx, (2) remove the eager wait from start_dma_transfer(), (3) root-cause and fix the mask buffer corruption — likely a cache coherency issue where LVGL reuses the mask buffer before DMA2D finishes reading it. Polling-based async (no interrupts) is sufficient since LVGL is single-threaded. Expected gain: 5-10% frame time reduction on complex screens. Defer until after higher-impact tasks.
