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

#include "lcd.h"

#include <lvgl/lvgl.h>

#include "bitmapbuffer.h"
#include "board.h"
#include "etx_lv_theme.h"
#if defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
#include "LvglWrapper.h"
#endif
#include "debug.h"
#include "os/time.h"
#if !LV_USE_GPU_STM32_DMA2D && !defined(SIMU)
#include "dma2d.h"
#endif
#if !defined(BOOT)
#include "os/sleep.h"
#endif

#if LV_MEM_CUSTOM == 0
char LVGL_MEM_BUFFER[LV_MEM_SIZE] __SDRAM __ALIGNED(16);

char* get_lvgl_mem(int nbytes)
{
  UNUSED(nbytes);
  return LVGL_MEM_BUFFER;
}
#endif

pixel_t LCD_FIRST_FRAME_BUFFER[DISPLAY_BUFFER_SIZE] __SDRAM;
pixel_t LCD_SECOND_FRAME_BUFFER[DISPLAY_BUFFER_SIZE] __SDRAM;

BitmapBuffer lcdBuffer1(BMP_RGB565, LCD_W, LCD_H,
                        (uint16_t*)LCD_FIRST_FRAME_BUFFER);
BitmapBuffer lcdBuffer2(BMP_RGB565, LCD_W, LCD_H,
                        (uint16_t*)LCD_SECOND_FRAME_BUFFER);

static BitmapBuffer* lcdFront = &lcdBuffer1;
static BitmapBuffer* lcd = &lcdBuffer2;

static lv_disp_draw_buf_t disp_buf;
static lv_disp_drv_t disp_drv;

volatile uint32_t LcdVBlankClock::counter = 0;

static LcdFlushManager g_lcdFlushMgr;

static void (*lcd_wait_cb)(lv_disp_drv_t*) = nullptr;
static void (*lcd_flush_cb)(lv_disp_drv_t*, uint16_t* buffer,
                            const rect_t& area) = nullptr;
static lcd_typed_flush_cb_t g_typedFlushCb = nullptr;

void lcdSetWaitCb(void (*cb)(lv_disp_drv_t*)) { lcd_wait_cb = cb; }

void lcdSetFlushCb(void (*cb)(lv_disp_drv_t*, uint16_t*, const rect_t&))
{
  LCD_ASSERT(!g_typedFlushCb,
             "lcdSetFlushCb: typed callback already registered");
  lcd_flush_cb = cb;
}

void lcdSetTypedFlushCb(lcd_typed_flush_cb_t cb)
{
  LCD_ASSERT(!lcd_flush_cb,
             "lcdSetTypedFlushCb: legacy callback already registered");
  g_typedFlushCb = cb;
}

static LcdInvalidatedAreas captureInvalidatedAreas()
{
  LcdInvalidatedAreas inv;
  lv_disp_t* disp = _lv_refr_get_disp_refreshing();
  if (disp) {
    for (int i = 0; i < disp->inv_p; i++) {
      if (!disp->inv_area_joined[i]) {
        inv.add(disp->inv_areas[i]);
      }
    }
  }
  return inv;
}

static void flushLcd(lv_disp_drv_t* disp_drv, const lv_area_t* area,
                     lv_color_t* color_p)
{
#if !defined(LCD_VERTICAL_INVERT) || defined(RADIO_F16)
#if defined(RADIO_F16)
  if (hardwareOptions.pcbrev > 0)
#endif
  {
    if (!lv_disp_flush_is_last(disp_drv)) {
      lv_disp_flush_ready(disp_drv);
      return;
    }
  }
#endif

#if defined(DEBUG_WINDOWS)
  if (area->x1 != 0 || area->x2 != LCD_W - 1 || area->y1 != 0 ||
      area->y2 != LCD_H - 1) {
    TRACE("partial refresh @ 0x%p {%d,%d,%d,%d}", color_p, area->x1, area->y1,
          area->x2, area->y2);
  } else {
    TRACE("full refresh @ 0x%p", color_p);
  }
#endif

  if (g_typedFlushCb) {
    g_lcdFlushMgr.onLvglFlush(disp_drv, area, color_p);
    return;
  }

  if (lcd_flush_cb) {
#if defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
    lvglAdaptiveUiPumpRecordFlush();
#endif

    rect_t copy_area = {area->x1, area->y1, area->x2 - area->x1 + 1,
                        area->y2 - area->y1 + 1};

    lcd_flush_cb(disp_drv, (uint16_t*)color_p, copy_area);
  }

  lv_disp_flush_ready(disp_drv);
}

static void lcdFlushWaitCb(lv_disp_drv_t* disp_drv)
{
  g_lcdFlushMgr.poll();
  if (g_lcdFlushMgr.isBusy()) {
    sleep_ms(1);
  }
  if (lcd_wait_cb) {
    lcd_wait_cb(disp_drv);
  }
}

void LcdFlushManager::onLvglFlush(lv_disp_drv_t* disp_drv,
                                  const lv_area_t* area, lv_color_t* color_p)
{
  while (state_ != State::Idle) {
    poll();
    sleep_ms(1);
#if defined(DEBUG) || defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
    diag_.busyFlushWaits++;
#endif
  }

  LvglFlushToken token(disp_drv);

  rect_t copy_area = {area->x1, area->y1, area->x2 - area->x1 + 1,
                      area->y2 - area->y1 + 1};

  LcdFlushChunk chunk;
  if (lv_disp_flush_is_last(disp_drv)) {
    chunk = LcdFlushChunk::finalChunk((uint16_t*)color_p, copy_area,
                                       captureInvalidatedAreas());
  } else {
    chunk = LcdFlushChunk::intermediate((uint16_t*)color_p, copy_area);
  }

  LcdFlushOutcome outcome = g_typedFlushCb(chunk);

  if (outcome.isReadyNow()) {
    token.complete();
#if defined(DEBUG) || defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
    diag_.asyncFlushesStarted++;
    diag_.asyncFlushesCompleted++;
#endif
  } else {
    LCD_ASSERT(outcome.kind == LcdFlushOutcome::Kind::AfterVBlank,
               "LcdFlushManager: invalid outcome kind");
    token_ = std::move(token);
    fence_ = outcome.fence;
    postVBlankWork_ = std::move(outcome.postVBlankWork);
    state_ = State::WaitingForVBlank;
    pendingStartMs_ = time_get_ms();
    timedOut_ = false;
#if defined(DEBUG) || defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
    diag_.asyncFlushesStarted++;
#endif
  }
}

void LcdFlushManager::poll()
{
  if (state_ != State::WaitingForVBlank) return;

  if (fence_.reached()) {
    completeFlush();
    return;
  }

  uint32_t elapsed = time_get_ms() - pendingStartMs_;
  if (elapsed >= LCD_FLUSH_VBLANK_TIMEOUT_MS) {
    forceCompleteFlush();
  }
}

bool LcdFlushManager::isBusy() const
{
  return state_ != State::Idle;
}

bool LcdFlushManager::hasTimedOut() const
{
  return timedOut_;
}

bool LcdFlushManager::isFramebufferSuspect() const
{
  return framebufferSuspect_;
}

void LcdFlushManager::clearFramebufferSuspect()
{
  framebufferSuspect_ = false;
}

LcdFlushDrainResult LcdFlushManager::drain(uint32_t timeoutMs)
{
  if (state_ == State::Idle) return LcdFlushDrainResult::Completed;

  uint32_t start = time_get_ms();

  while (state_ != State::Idle) {
    poll();
    if (state_ == State::Idle) {
      return hasTimedOut() ? LcdFlushDrainResult::TimedOut
                           : LcdFlushDrainResult::Completed;
    }

    if (time_get_ms() - start >= timeoutMs) {
      return LcdFlushDrainResult::StillBusy;
    }

    sleep_ms(1);
  }

  return state_ == State::Idle ? LcdFlushDrainResult::Completed
                               : LcdFlushDrainResult::StillBusy;
}

void LcdFlushManager::onVBlankTick() {}

void LcdFlushManager::completeFlush()
{
  LCD_ASSERT(state_ == State::WaitingForVBlank,
             "completeFlush: not in WaitingForVBlank");
  LCD_ASSERT(token_.isLive(), "completeFlush: no live token");

  if (postVBlankWork_.hasWork()) {
    postVBlankWork_.run();
  }

  token_.complete();
  state_ = State::Idle;
  framebufferSuspect_ = false;

#if defined(DEBUG) || defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
  diag_.asyncFlushesCompleted++;
  uint32_t duration = time_get_ms() - pendingStartMs_;
  if (duration > diag_.maxPendingDurationMs) {
    diag_.maxPendingDurationMs = duration;
  }
#endif
}

void LcdFlushManager::forceCompleteFlush()
{
  LCD_ASSERT(state_ == State::WaitingForVBlank,
             "forceCompleteFlush: not in WaitingForVBlank");

#if defined(DEBUG) || defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
  diag_.vblankTimeouts++;
  TRACE_WARNING("LcdFlushManager: vblank timeout (%ums), completing",
                (unsigned)(time_get_ms() - pendingStartMs_));
#endif

  // Run post-vblank work anyway on timeout — it's safer to attempt
  // partial sync than to leave buffers completely stale.
  // The Horus back-buffer sync will copy what it can.
  if (postVBlankWork_.hasWork()) {
    postVBlankWork_.run();
  }

  timedOut_ = true;
  framebufferSuspect_ = true;

  if (token_.isLive()) {
    token_.complete();
  }
  state_ = State::Idle;

#if defined(DEBUG) || defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
  diag_.asyncFlushesCompleted++;
  uint32_t duration = time_get_ms() - pendingStartMs_;
  if (duration > diag_.maxPendingDurationMs) {
    diag_.maxPendingDurationMs = duration;
  }
#endif
}

void lcdFlushPoll()
{
  g_lcdFlushMgr.poll();
}

bool lcdFlushIsBusy()
{
  return g_lcdFlushMgr.isBusy();
}

LcdFlushDrainResult lcdFlushDrain(uint32_t timeoutMs)
{
  return g_lcdFlushMgr.drain(timeoutMs);
}

void lcdVBlankTickFromIsr()
{
  LcdVBlankClock::tickFromIsr();
  g_lcdFlushMgr.onVBlankTick();
}

bool lvglRefreshNowIfIdle()
{
  lcdFlushPoll();
  if (lcdFlushIsBusy()) return false;
  lv_refr_now(nullptr);
  lcdFlushPoll();
  return !lcdFlushIsBusy();
}

bool lvglRefreshNowAndDrain(uint32_t timeoutMs)
{
  auto before = lcdFlushDrain(timeoutMs);
  if (before != LcdFlushDrainResult::Completed) return false;

  lv_refr_now(nullptr);

  auto after = lcdFlushDrain(timeoutMs);
  return after == LcdFlushDrainResult::Completed;
}

static void clear_frame_buffers()
{
  memset(LCD_FIRST_FRAME_BUFFER, 0, sizeof(LCD_FIRST_FRAME_BUFFER));
  memset(LCD_SECOND_FRAME_BUFFER, 0, sizeof(LCD_SECOND_FRAME_BUFFER));
}

static void init_lvgl_disp_drv()
{
  lv_disp_draw_buf_init(&disp_buf, lcdFront->getData(), lcd->getData(),
                        LCD_W * LCD_H);
  lv_disp_drv_init(&disp_drv); /*Basic initialization*/

  disp_drv.draw_buf = &disp_buf; /*Set an initialized buffer*/
  disp_drv.flush_cb = flushLcd;  /*Set a flush callback to draw to the display*/
  disp_drv.wait_cb = lcdFlushWaitCb; /*Set a wait callback that polls the manager*/

  disp_drv.hor_res = LCD_W; /*Set the horizontal resolution in pixels*/
  disp_drv.ver_res = LCD_H; /*Set the vertical resolution in pixels*/
  disp_drv.full_refresh = 0;

#if !defined(LCD_VERTICAL_INVERT)
  disp_drv.direct_mode = 1;
#elif defined(RADIO_F16)
  disp_drv.direct_mode = (hardwareOptions.pcbrev > 0) ? 1 : 0;
#else
  disp_drv.direct_mode = 0;
#endif
}

void lcdInitDisplayDriver()
{
  static bool lcdDriverStarted = false;
  // we already have a display: exit
  if (lcdDriverStarted) return;
  lcdDriverStarted = true;

#if !LV_USE_GPU_STM32_DMA2D && !defined(SIMU)
  DMAInit();
#endif

  // Full LVGL init in firmware mode
  lv_init();
  // Initialise styles
  useMainStyle();

  // Clear buffers first
  clear_frame_buffers();
  lcdSetInitalFrameBuffer(lcdFront->getData());

  // Init hardware LCD driver
  lcdInit();
  backlightInit();

  init_lvgl_disp_drv();

  // Register the driver and save the created display object
  lv_disp_drv_register(&disp_drv);

  // remove all styles on default screen (makes it transparent as well)
  lv_obj_remove_style_all(lv_scr_act());
}

void lcdFlushed()
{
}

#if defined(SIMU)
void lcdRefresh() {}
#endif
