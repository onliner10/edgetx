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

#include "lvgl/lvgl.h"

class LvglWrapper
{
 public:
  static LvglWrapper* instance();

  // Called from UI task: executes the LVGL timer handler 
  uint32_t run();

  // Called from UI task: true while input/scroll/animation work can benefit
  // from servicing LVGL before the next 50 ms menu tick.
  bool hasAdaptiveWork() const;
  bool getNextRunDelay(uint32_t& delay) const;

 protected:
  static LvglWrapper *_instance;

  LvglWrapper();
  ~LvglWrapper() {}

  uint32_t nextRunAt = 0;
  bool nextRunKnown = false;
};

#if defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
struct LvglAdaptiveUiPumpStats
{
  volatile uint32_t handlerCount;
  volatile uint32_t handlerDurationMs;
  volatile uint32_t handlerMaxDurationMs;
  volatile uint32_t flushCount;
  volatile uint32_t vblankWaitCount;
  volatile uint32_t vblankWaitMs;
  volatile uint32_t vblankWaitMaxMs;
  volatile uint32_t menuCycleMaxMs;
};

extern LvglAdaptiveUiPumpStats lvglAdaptiveUiPumpStats;
void lvglAdaptiveUiPumpRecordFlush();
void lvglAdaptiveUiPumpRecordVblankWait(uint32_t durationMs);
#endif
