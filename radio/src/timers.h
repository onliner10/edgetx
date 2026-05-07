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

#define TMR_OFF      0
#define TMR_RUNNING  1
#define TMR_NEGATIVE 2
#define TMR_STOPPED  3

typedef int32_t tmrval_t;
typedef uint32_t tmrstart_t;
typedef int16_t tmrmode_t;
#define TIMER_MAX     (0xffffff/2)

#define TIMER_MIN     (tmrval_t(-TIMER_MAX-1))

struct TimerState {
  uint16_t cnt;
  uint16_t sum;
  uint8_t  state;
  tmrval_t  val;
  uint8_t  val_10ms;
};

#if defined(TIMERS)
extern TimerState timersStates[TIMERS];

inline tmrval_t getTimerStateValue(uint8_t idx)
{
  return __atomic_load_n(&timersStates[idx].val, __ATOMIC_RELAXED);
}

inline void setTimerStateValue(uint8_t idx, tmrval_t value)
{
  __atomic_store_n(&timersStates[idx].val, value, __ATOMIC_RELAXED);
}
#endif

void timerReset(uint8_t idx);

void timerSet(int idx, int val);

void saveTimers();
void restoreTimers();

void evalTimers(int16_t throttle, uint8_t tick10ms);

int16_t throttleSource2Source(int16_t thrSrc);
int16_t source2ThrottleSource(int16_t src);
