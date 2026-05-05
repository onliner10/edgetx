/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
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

#include "gtests.h"

#ifndef __has_feature
#define __has_feature(x) 0
#endif

// MSan provides its own global nothrow new/delete interceptors. This
// allocation-failure injection test relies on replacing them, so keep it out of
// MemorySanitizer builds and let MSan own allocation tracking.
#if defined(COLORLCD) && !GTEST_OS_WINDOWS && !__has_feature(memory_sanitizer)

#include <atomic>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <new>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

std::atomic<unsigned> nothrowAllocationsToFail{0};
int nothrowFailureNotifyFd = -1;

}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
  unsigned remaining = nothrowAllocationsToFail.load(std::memory_order_relaxed);
  while (remaining != 0) {
    if (nothrowAllocationsToFail.compare_exchange_weak(
            remaining, remaining - 1, std::memory_order_relaxed)) {
      if (nothrowFailureNotifyFd >= 0) {
        char byte = 1;
        (void)write(nothrowFailureNotifyFd, &byte, sizeof(byte));
      }
      return nullptr;
    }
  }

  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
  ::operator delete(ptr);
}

TEST(ColorAlerts, allocationFailureDoesNotReturnWithoutAcknowledgement)
{
  int pipeFds[2];
  ASSERT_EQ(pipe(pipeFds), 0);
  ASSERT_EQ(fcntl(pipeFds[0], F_SETFL, O_NONBLOCK), 0);

  nothrowFailureNotifyFd = pipeFds[1];
  nothrowAllocationsToFail.store(1, std::memory_order_relaxed);

  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    close(pipeFds[0]);
    raiseAlert("Safety", "Allocation failed", STR_PRESS_ANY_KEY_TO_SKIP, 0);
    _exit(0);
  }

  nothrowAllocationsToFail.store(0, std::memory_order_relaxed);
  nothrowFailureNotifyFd = -1;
  close(pipeFds[1]);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  char byte = 0;
  EXPECT_EQ(read(pipeFds[0], &byte, sizeof(byte)), 1);
  close(pipeFds[0]);

  int status = 0;
  const pid_t waitResult = waitpid(pid, &status, WNOHANG);
  if (waitResult == pid) {
    FAIL() << "raiseAlert returned without pilot acknowledgement after OOM";
  }
  ASSERT_EQ(waitResult, 0);

  kill(pid, SIGTERM);
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
}

#endif
