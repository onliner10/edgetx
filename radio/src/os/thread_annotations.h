/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once

#if defined(__clang__)
  #define ETX_THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
  #define ETX_THREAD_ANNOTATION_ATTRIBUTE__(x)
#endif

#define ETX_CAPABILITY(x) ETX_THREAD_ANNOTATION_ATTRIBUTE__(capability(x))
#define ETX_SCOPED_CAPABILITY ETX_THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)
#define ETX_ACQUIRE(...) ETX_THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))
#define ETX_RELEASE(...) ETX_THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))
#define ETX_TRY_ACQUIRE(...) ETX_THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_capability(__VA_ARGS__))
#define ETX_GUARDED_BY(x) ETX_THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))
#define ETX_NO_THREAD_SAFETY_ANALYSIS ETX_THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)
