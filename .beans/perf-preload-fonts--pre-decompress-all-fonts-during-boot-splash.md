---
# perf-preload-fonts
title: Pre-decompress all fonts during boot splash
status: todo
type: task
priority: high
parent: perf-rendering
created_at: 2026-05-08T10:26:27Z
updated_at: 2026-05-08T10:26:27Z
---

Fonts are LZ4-compressed C arrays decompressed lazily on first access via decompressFont() in fonts.cpp. When a user first opens a screen that needs a font size not yet loaded, LZ4_decompress_safe() runs during the interaction, causing a visible frame stutter. Pre-decompress all font sizes for the active language during the boot splash sequence, before the main UI becomes interactive. For ALL_LANGUAGES builds, preload only the active language's fonts at boot; defer other languages to first-access as today.
