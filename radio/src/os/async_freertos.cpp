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

#include "task.h"
#include "async.h"

#include <FreeRTOS/include/FreeRTOS.h>
#include <FreeRTOS/include/timers.h>

static inline bool async_try_claim(AsyncExclusiveFlag* excl_flag)
{
  return !excl_flag || excl_flag->tryClaim();
}

static inline void async_clear_claim(AsyncExclusiveFlag* excl_flag)
{
  if (excl_flag) excl_flag->clear();
}

bool async_call(async_func_t func, AsyncExclusiveFlag* excl_flag, void* param1,
                uint32_t param2)
{
  if (!async_try_claim(excl_flag)) return false;

  if (scheduler_is_running()) {
    BaseType_t xReturn = xTimerPendFunctionCall(func, param1, param2, 0);

    if (xReturn != pdPASS) async_clear_claim(excl_flag);

    return xReturn == pdPASS;
  }

  func(param1, param2);
  async_clear_claim(excl_flag);
  return true;
}

bool async_call_isr(async_func_t func, AsyncExclusiveFlag* excl_flag, void* param1,
                    uint32_t param2)
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  if (!async_try_claim(excl_flag)) return false;

  if (scheduler_is_running()) {
    BaseType_t xReturn = xTimerPendFunctionCallFromISR(
        func, param1, param2, &xHigherPriorityTaskWoken);

    if (xReturn != pdPASS) async_clear_claim(excl_flag);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    return xReturn == pdPASS;
  }

  func(param1, param2);
  async_clear_claim(excl_flag);
  return true;
}
