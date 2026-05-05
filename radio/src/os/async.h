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

#include <atomic>
#include <stdint.h>
#include <stdbool.h>

typedef void (*async_func_t)(void* param1, uint32_t param2);

class AsyncExclusiveFlag
{
  public:
    AsyncExclusiveFlag() : queued(false) {}

    AsyncExclusiveFlag(const AsyncExclusiveFlag&) = delete;
    AsyncExclusiveFlag& operator=(const AsyncExclusiveFlag&) = delete;

    bool tryClaim()
    {
      bool expected = false;
      return queued.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire);
    }

    void clear()
    {
      queued.store(false, std::memory_order_release);
    }

  private:
    static_assert(ATOMIC_BOOL_LOCK_FREE == 2,
                  "AsyncExclusiveFlag must be lock-free");

    std::atomic_bool queued;
};

// schedule a function call for later
// return true if the async call could be stacked, false otherwise
// If excl_flag is provided, the queued function must clear it when it is ready
// to accept another pending call.
bool async_call(async_func_t func, AsyncExclusiveFlag* excl_flag, void* param1,
                uint32_t param2);

// schedule a function call for later (interrupt handler)
bool async_call_isr(async_func_t func, AsyncExclusiveFlag* excl_flag, void* param1,
                    uint32_t param2);
