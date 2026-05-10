/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#pragma once

#include "edgetx_types.h"

#include "colors.h"

#include <lvgl/lvgl.h>
#include <cstddef>
#include <cstdint>
#include <utility>

#if !defined(LCD_FLUSH_INVALIDATED_AREAS_MAX)
#define LCD_FLUSH_INVALIDATED_AREAS_MAX LV_INV_BUF_SIZE
#endif

#if !defined(LCD_FLUSH_VBLANK_TIMEOUT_MS)
#define LCD_FLUSH_VBLANK_TIMEOUT_MS 50
#endif

struct _lv_disp_drv_t;
typedef _lv_disp_drv_t lv_disp_drv_t;

#if defined(DEBUG) || defined(SIMU)
#define LCD_ASSERT(cond, msg) \
  do {                        \
    if (!(cond)) {            \
      while (1)               \
        ;                     \
    }                         \
  } while (0)
#else
#define LCD_ASSERT(cond, msg) \
  do {                        \
  } while (0)
#endif

struct LcdInvalidatedAreas {
  uint8_t count = 0;
  bool overflowed = false;
  lv_area_t areas[LCD_FLUSH_INVALIDATED_AREAS_MAX];

  void add(const lv_area_t& area)
  {
    if (count < LCD_FLUSH_INVALIDATED_AREAS_MAX) {
      lv_area_copy(&areas[count], &area);
      ++count;
    } else {
      overflowed = true;
    }
  }

  void clear()
  {
    count = 0;
    overflowed = false;
  }

  bool empty() const { return count == 0 && !overflowed; }
};

struct LcdVBlankClock {
  static volatile uint32_t counter;

  static uint32_t now()
  {
    return __atomic_load_n(&counter, __ATOMIC_ACQUIRE);
  }

  static void tickFromIsr()
  {
    __atomic_add_fetch(&counter, 1, __ATOMIC_RELEASE);
  }
};

class LcdVBlankFence {
public:
  LcdVBlankFence() = default;
  explicit LcdVBlankFence(uint32_t target) : target_(target) {}

  uint32_t target() const { return target_; }

  bool reached() const
  {
    uint32_t cur = LcdVBlankClock::now();
    auto diff = static_cast<int32_t>(cur - target_);
    return diff >= 0;
  }

  static LcdVBlankFence afterNext()
  {
    return LcdVBlankFence(LcdVBlankClock::now() + 1);
  }

private:
  uint32_t target_ = 0;
};

class LvglFlushToken {
public:
  LvglFlushToken() : drv_(nullptr) {}
  explicit LvglFlushToken(lv_disp_drv_t* drv) : drv_(drv) {}

  LvglFlushToken(const LvglFlushToken&) = delete;
  LvglFlushToken& operator=(const LvglFlushToken&) = delete;

  LvglFlushToken(LvglFlushToken&& o) noexcept : drv_(o.drv_) { o.drv_ = nullptr; }
  LvglFlushToken& operator=(LvglFlushToken&& o) noexcept
  {
    if (this != &o) {
      LCD_ASSERT(!drv_, "LvglFlushToken: move-assign over live token");
      drv_ = o.drv_;
      o.drv_ = nullptr;
    }
    return *this;
  }

  ~LvglFlushToken()
  {
    LCD_ASSERT(drv_ == nullptr,
               "LvglFlushToken: destroyed without completing flush");
  }

  void complete()
  {
    LCD_ASSERT(drv_, "LvglFlushToken: double complete");
    lv_disp_flush_ready(drv_);
    drv_ = nullptr;
  }

  lv_disp_drv_t* driver() const { return drv_; }

  bool isLive() const { return drv_ != nullptr; }

private:
  lv_disp_drv_t* drv_;
};

struct LcdFlushChunk {
  enum class Kind : uint8_t { Intermediate, Final };

  Kind kind;
  uint16_t* pixels;
  rect_t area;

  LcdInvalidatedAreas invalidatedAreas;

  static LcdFlushChunk intermediate(uint16_t* px, const rect_t& a)
  {
    LcdFlushChunk c;
    c.kind = Kind::Intermediate;
    c.pixels = px;
    c.area = a;
    return c;
  }

  static LcdFlushChunk finalChunk(uint16_t* px, const rect_t& a,
                                   LcdInvalidatedAreas inv)
  {
    LcdFlushChunk c;
    c.kind = Kind::Final;
    c.pixels = px;
    c.area = a;
    c.invalidatedAreas = std::move(inv);
    return c;
  }

  bool isFinal() const { return kind == Kind::Final; }
};

class LcdPostVBlankWork {
public:
  using Callable = void (*)(void* ctx);

  LcdPostVBlankWork() : fn_(nullptr), ctx_(nullptr) {}

  LcdPostVBlankWork(Callable fn, void* ctx) : fn_(fn), ctx_(ctx) {}

  LcdPostVBlankWork(const LcdPostVBlankWork&) = delete;
  LcdPostVBlankWork& operator=(const LcdPostVBlankWork&) = delete;

  LcdPostVBlankWork(LcdPostVBlankWork&& o) noexcept : fn_(o.fn_), ctx_(o.ctx_)
  {
    o.fn_ = nullptr;
    o.ctx_ = nullptr;
  }

  LcdPostVBlankWork& operator=(LcdPostVBlankWork&& o) noexcept
  {
    if (this != &o) {
      fn_ = o.fn_;
      ctx_ = o.ctx_;
      o.fn_ = nullptr;
      o.ctx_ = nullptr;
    }
    return *this;
  }

  bool hasWork() const { return fn_ != nullptr; }

  void run()
  {
    if (fn_) {
      fn_(ctx_);
      fn_ = nullptr;
      ctx_ = nullptr;
    }
  }

private:
  Callable fn_;
  void* ctx_;
};

struct LcdFlushOutcome {
  enum class Kind : uint8_t { ReadyNow, AfterVBlank };

  Kind kind;

  LcdVBlankFence fence;
  LcdPostVBlankWork postVBlankWork;

  static LcdFlushOutcome readyNow()
  {
    LcdFlushOutcome o;
    o.kind = Kind::ReadyNow;
    return o;
  }

  static LcdFlushOutcome afterVBlank(LcdVBlankFence f,
                                     LcdPostVBlankWork w = LcdPostVBlankWork())
  {
    LcdFlushOutcome o;
    o.kind = Kind::AfterVBlank;
    o.fence = f;
    o.postVBlankWork = std::move(w);
    return o;
  }

  bool isReadyNow() const { return kind == Kind::ReadyNow; }
  bool isAfterVBlank() const { return kind == Kind::AfterVBlank; }
};

using lcd_typed_flush_cb_t = LcdFlushOutcome (*)(const LcdFlushChunk& chunk);

class RotatedFrameBuffers {
public:
  enum class Visible : uint8_t { Buf1, Buf2 };

  RotatedFrameBuffers() = delete;
  RotatedFrameBuffers(uint16_t* buf1, uint16_t* buf2)
    : buf1_(buf1), buf2_(buf2), visible_(Visible::Buf1)
  {
    LCD_ASSERT(buf1 != nullptr, "RotatedFrameBuffers: null buf1");
    LCD_ASSERT(buf2 != nullptr, "RotatedFrameBuffers: null buf2");
    LCD_ASSERT(buf1 != buf2, "RotatedFrameBuffers: buf1 == buf2");
  }

  RotatedFrameBuffers(const RotatedFrameBuffers&) = delete;
  RotatedFrameBuffers& operator=(const RotatedFrameBuffers&) = delete;

  uint16_t* frontBuf() const
  {
    return visible_ == Visible::Buf1 ? buf1_ : buf2_;
  }

  uint16_t* backBuf() const
  {
    return visible_ == Visible::Buf1 ? buf2_ : buf1_;
  }

  void swap()
  {
    visible_ = (visible_ == Visible::Buf1) ? Visible::Buf2 : Visible::Buf1;
  }

private:
  uint16_t* buf1_;
  uint16_t* buf2_;
  Visible visible_;
};

struct HorusBackBufferSync {
  uint16_t* front;
  uint16_t* back;
  LcdInvalidatedAreas areas;

  static void run(void* ctx);
};

#if defined(DEBUG) || defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
struct LcdFlushDiagnostics {
  uint32_t asyncFlushesStarted = 0;
  uint32_t asyncFlushesCompleted = 0;
  uint32_t maxPendingDurationMs = 0;
  uint32_t vblankTimeouts = 0;
  uint32_t busyFlushWaits = 0;
};
#endif

enum class LcdFlushDrainResult : uint8_t {
  Completed,
  TimedOut,
  StillBusy,
};

class LcdFlushManager {
public:
  LcdFlushManager() = default;
  LcdFlushManager(const LcdFlushManager&) = delete;
  LcdFlushManager& operator=(const LcdFlushManager&) = delete;

  void onLvglFlush(lv_disp_drv_t* disp_drv, const lv_area_t* area,
                   lv_color_t* color_p);
  void poll();
  bool isBusy() const;
  bool hasTimedOut() const;
  bool isFramebufferSuspect() const;
  void clearFramebufferSuspect();
  LcdFlushDrainResult drain(uint32_t timeoutMs);
  void onVBlankTick();

#if defined(DEBUG) || defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
  const LcdFlushDiagnostics& diagnostics() const { return diag_; }
#endif

private:
  enum class State : uint8_t { Idle, WaitingForVBlank };

  State state_ = State::Idle;
  LvglFlushToken token_;
  LcdVBlankFence fence_;
  LcdPostVBlankWork postVBlankWork_;
  uint32_t pendingStartMs_ = 0;
  bool timedOut_ = false;
  bool framebufferSuspect_ = false;

#if defined(DEBUG) || defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
  LcdFlushDiagnostics diag_;
#endif

  void completeFlush();
  void forceCompleteFlush();
};

void lcdSetWaitCb(void (*cb)(lv_disp_drv_t*));
void lcdSetFlushCb(void (*cb)(lv_disp_drv_t*, uint16_t*, const rect_t&));
void lcdSetTypedFlushCb(lcd_typed_flush_cb_t cb);

void lcdFlushPoll();
bool lcdFlushIsBusy();

LcdFlushDrainResult lcdFlushDrain(uint32_t timeoutMs);
void lcdVBlankTickFromIsr();

void lcdInitDisplayDriver();
void lcdClear();
void lcdInitDirectDrawing();
void lcdRefresh();
void lcdFlushed();

bool lvglRefreshNowIfIdle();
bool lvglRefreshNowAndDrain(uint32_t timeoutMs = LCD_FLUSH_VBLANK_TIMEOUT_MS * 2);

#define DISPLAY_PIXELS_COUNT (LCD_W * LCD_H)
#define DISPLAY_BUFFER_SIZE (DISPLAY_PIXELS_COUNT)

#if defined(BOOT)
#define BLINK_ON_PHASE (0)
#else
#define BLINK_ON_PHASE (g_blinkTmr10ms & (1 << 6))
#define SLOW_BLINK_ON_PHASE (g_blinkTmr10ms & (1 << 7))
#endif