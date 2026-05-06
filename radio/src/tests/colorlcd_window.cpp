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

#include <sys/wait.h>
#include <unistd.h>

#include <new>

#include "mainwindow.h"

bool etxCreateObjectAllocationFailureReturnsNullForTest();
bool etxLabelAllocationFailureReturnsNullForTest();
bool etxStyleHelpersIgnoreNullObjectForTest();
bool windowObjectAllocationFailureLeavesNoLvObjForTest();
bool formFieldObjectAllocationFailureFailsClosedForTest();
bool childOfUnavailableParentFailsClosedForTest();
bool adoptLiveFailedChildDetachesFromParentForTest();
bool requiredWindowBuilderFailureFailsOwnerClosedForTest();
bool attachToUnavailableParentPreservesExistingParentForTest();
bool unavailableWindowDirectClickDoesNotBubbleForTest();
bool windowDelayedLoadGatesLoadedTasksForTest();
bool buttonMatrixObjectAllocationFailureFailsClosedForTest();
bool tableFieldObjectAllocationFailureFailsClosedForTest();
bool tableFieldInvalidSelectionClearsWithoutScrollForTest();
bool tableFieldSelectMovesAcrossColumnsForTest();
bool toggleSwitchObjectAllocationFailureFailsClosedForTest();
bool textEditTextAreaAllocationFailureDoesNotCacheDeadEditorForTest();
bool numberEditNumberAreaAllocationFailureDoesNotCacheDeadEditorForTest();
bool textKeyboardWindowAllocationFailureDoesNotCacheDeadKeyboardForTest();
bool textKeyboardKeypadAllocationFailureDoesNotCacheDeadKeyboardForTest();
bool numberKeyboardWindowAllocationFailureDoesNotCacheDeadKeyboardForTest();
bool numberKeyboardKeypadAllocationFailureDoesNotCacheDeadKeyboardForTest();
bool progressBarAllocationFailureFailsClosedForTest();
bool listBoxObjectAllocationFailureFailsClosedForTest();
bool lvglWrapperUnavailableMainWindowIsNotLoadedForTest();
bool fullScreenDialogMessageLabelCreateFailureFailsClosedForTest();

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

TEST(ColorEtxTheme, LabelAllocationFailureReturnsNull)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(etxLabelAllocationFailureReturnsNullForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorEtxTheme, StyleHelpersIgnoreNullObject)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(etxStyleHelpersIgnoreNullObjectForTest() ? 0 : 1);
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

TEST(ColorWindow, FormFieldObjectAllocationFailureFailsClosed)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(formFieldObjectAllocationFailureFailsClosedForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, ChildOfUnavailableParentFailsClosed)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(childOfUnavailableParentFailsClosedForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, AdoptLiveFailedChildDetachesFromParent)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(adoptLiveFailedChildDetachesFromParentForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, RequiredWindowBuilderFailureFailsOwnerClosed)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(requiredWindowBuilderFailureFailsOwnerClosedForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, AttachToUnavailableParentPreservesExistingParent)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(attachToUnavailableParentPreservesExistingParentForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, UnavailableWindowDirectClickDoesNotBubble)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(unavailableWindowDirectClickDoesNotBubbleForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, DelayedLoadGatesLoadedTasks)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(windowDelayedLoadGatesLoadedTasksForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, ButtonMatrixObjectAllocationFailureFailsClosed)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(buttonMatrixObjectAllocationFailureFailsClosedForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, TableFieldObjectAllocationFailureFailsClosed)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(tableFieldObjectAllocationFailureFailsClosedForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, TableFieldInvalidSelectionClearsWithoutScroll)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(tableFieldInvalidSelectionClearsWithoutScrollForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, TableFieldSelectMovesAcrossColumns)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(tableFieldSelectMovesAcrossColumnsForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, ToggleSwitchObjectAllocationFailureFailsClosed)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(toggleSwitchObjectAllocationFailureFailsClosedForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, TextEditTextAreaAllocationFailureDoesNotCacheDeadEditor)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(textEditTextAreaAllocationFailureDoesNotCacheDeadEditorForTest() ? 0
                                                                           : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, NumberEditNumberAreaAllocationFailureDoesNotCacheDeadEditor)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(numberEditNumberAreaAllocationFailureDoesNotCacheDeadEditorForTest()
              ? 0
              : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, TextKeyboardWindowAllocationFailureDoesNotCacheDeadKeyboard)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(textKeyboardWindowAllocationFailureDoesNotCacheDeadKeyboardForTest()
              ? 0
              : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, TextKeyboardKeypadAllocationFailureDoesNotCacheDeadKeyboard)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(textKeyboardKeypadAllocationFailureDoesNotCacheDeadKeyboardForTest()
              ? 0
              : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, NumberKeyboardWindowAllocationFailureDoesNotCacheDeadKeyboard)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(numberKeyboardWindowAllocationFailureDoesNotCacheDeadKeyboardForTest()
              ? 0
              : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, NumberKeyboardKeypadAllocationFailureDoesNotCacheDeadKeyboard)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(numberKeyboardKeypadAllocationFailureDoesNotCacheDeadKeyboardForTest()
              ? 0
              : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, ProgressBarAllocationFailureFailsClosed)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(progressBarAllocationFailureFailsClosedForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, ListBoxObjectAllocationFailureFailsClosed)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(listBoxObjectAllocationFailureFailsClosedForTest() ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, FullScreenDialogMessageLabelCreateFailureFailsClosed)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(fullScreenDialogMessageLabelCreateFailureFailsClosedForTest() ? 0
                                                                        : 1);
  }

  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ColorWindow, UnavailableMainWindowIsNotLoaded)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(lvglWrapperUnavailableMainWindowIsNotLoadedForTest() ? 0 : 1);
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
