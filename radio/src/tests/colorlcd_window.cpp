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

#if defined(COLORLCD) && !GTEST_OS_WINDOWS

#include "mainwindow.h"

#include <new>
#include <sys/wait.h>
#include <unistd.h>

bool etxCreateObjectAllocationFailureReturnsNullForTest();
bool windowObjectAllocationFailureLeavesNoLvObjForTest();

TEST(ColorEtxTheme, ObjectAllocationFailureReturnsNull)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(etxCreateObjectAllocationFailureReturnsNullForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, ObjectAllocationFailureLeavesNoLvObj)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(windowObjectAllocationFailureLeavesNoLvObjForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindowLayers, PoppingMiddleLayerKeepsTopLayerActive)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    Window* parent = MainWindow::instance();
    auto bottom = new (std::nothrow) Window(parent, {0, 0, 10, 10});
    auto middle = new (std::nothrow) Window(parent, {0, 0, 10, 10});
    auto top = new (std::nothrow) Window(parent, {0, 0, 10, 10});

    if (!bottom || !middle || !top) _exit(2);

    bottom->pushLayer();
    lv_group_t* bottomGroup = lv_group_get_default();
    middle->pushLayer();
    top->pushLayer();
    lv_group_t* topGroup = lv_group_get_default();

    if (Window::topWindow() != top) _exit(3);

    middle->popLayer();

    if (Window::topWindow() != top) _exit(1);
    if (lv_group_get_default() != topGroup) _exit(4);

    top->popLayer();

    if (Window::topWindow() != bottom) _exit(5);
    if (lv_group_get_default() != bottomGroup) _exit(6);

    _exit(0);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

#endif
