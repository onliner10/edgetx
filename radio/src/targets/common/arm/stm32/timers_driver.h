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

class Tmr10msCounter
{
  public:
    constexpr Tmr10msCounter() : _value(0) {}

    Tmr10msCounter(const Tmr10msCounter&) = delete;
    Tmr10msCounter& operator=(const Tmr10msCounter&) = delete;

    tmr10ms_t load() const
    {
      return __atomic_load_n(&_value, __ATOMIC_RELAXED);
    }

    void store(tmr10ms_t newValue)
    {
      __atomic_store_n(&_value, newValue, __ATOMIC_RELAXED);
    }

    tmr10ms_t increment()
    {
      return __atomic_add_fetch(&_value, 1, __ATOMIC_RELAXED);
    }

  private:
    tmr10ms_t _value;
};

extern Tmr10msCounter g_tmr10ms;
static inline tmr10ms_t get_tmr10ms() { return g_tmr10ms.load(); }
static inline void set_tmr10ms(tmr10ms_t value) { g_tmr10ms.store(value); }
static inline tmr10ms_t inc_tmr10ms() { return g_tmr10ms.increment(); }
static inline uint8_t get_tmr10ms_low8() { return uint8_t(get_tmr10ms()); }

extern "C" tmr10ms_t timersGet10msTick();

void watchdogSuspend(uint32_t timeout);

void timersInit();

uint32_t timersGetMsTick();
uint32_t timersGetUsTick();

// declared "weak", to be implemented by application
void per5ms();
void per10ms();
