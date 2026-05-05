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

#if defined(COLORLCD) && defined(SIMU) && !GTEST_OS_WINDOWS

#include "bitmapbuffer.h"

#include <sys/wait.h>
#include <unistd.h>

void bitmapBufferForceDataMallocFailureForTest(bool force);

TEST(ColorBitmapBuffer, FailedPixelAllocationInvalidatesBuffer)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    bitmapBufferForceDataMallocFailureForTest(true);
    BitmapBuffer bitmap(BMP_RGB565, 10, 10);
    bitmapBufferForceDataMallocFailureForTest(false);

    if (bitmap.getData()) _exit(2);
    if (bitmap.width() != 0 || bitmap.height() != 0) _exit(1);

    _exit(0);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

#endif
