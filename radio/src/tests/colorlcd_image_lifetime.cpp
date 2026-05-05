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

#include "bitmapbuffer.h"

#define protected public
#include "mainwindow.h"
#include "static.h"
#undef protected

#include <sys/wait.h>
#include <unistd.h>

#if defined(SIMU)
bool mainWindowBackgroundCanvasCreateFailureLeavesNoCanvasForTest();
#endif

namespace {

constexpr const char* VALID_IMAGE = "images/color/edgetx.png";
constexpr const char* MISSING_IMAGE = "images/color/missing-image.png";

const void* canvasData(lv_obj_t* canvas)
{
  lv_img_dsc_t* image = canvas ? lv_canvas_get_img(canvas) : nullptr;
  return image ? image->data : nullptr;
}

void expectChildSuccess(pid_t pid)
{
  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child process did not exit normally";
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

}  // namespace

#if defined(SIMU)
TEST(ColorImageLifetime, MainWindowCanvasCreateFailureLeavesNoCanvas)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    alarm(2);
    _exit(mainWindowBackgroundCanvasCreateFailureLeavesNoCanvasForTest() ? 0
                                                                        : 1);
  }

  expectChildSuccess(pid);
}
#endif

TEST(ColorImageLifetime, MainWindowKeepsBackgroundOnFailedReplacement)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    MainWindow* window = MainWindow::instance();
    std::string validImage(VALID_IMAGE);
    if (!window->setBackgroundImage(validImage)) _exit(2);

    const BitmapBuffer* bitmap = window->backgroundBitmap;
    const void* bitmapData = bitmap ? bitmap->getData() : nullptr;
    if (!bitmapData || canvasData(window->background) != bitmapData) {
      _exit(3);
    }

    std::string missingImage(MISSING_IMAGE);
    if (window->setBackgroundImage(missingImage)) _exit(4);
    if (window->backgroundBitmap != bitmap) _exit(1);
    if (canvasData(window->background) != bitmapData) _exit(5);

    _exit(0);
  }

  expectChildSuccess(pid);
}

TEST(ColorImageLifetime, StaticBitmapKeepsImageOnFailedReplacement)
{
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);

  if (pid == 0) {
    StaticBitmap bitmap(MainWindow::instance(), {0, 0, 20, 20});
    bitmap.setSource(VALID_IMAGE);

    BitmapBuffer* source = bitmap.img;
    const void* sourceData = source ? source->getData() : nullptr;
    if (!bitmap.hasImage() || !sourceData ||
        canvasData(bitmap.canvas) != sourceData) {
      _exit(2);
    }

    bitmap.setSource(MISSING_IMAGE);

    if (!bitmap.hasImage()) _exit(1);
    if (bitmap.img != source) _exit(3);
    if (canvasData(bitmap.canvas) != sourceData) _exit(4);

    _exit(0);
  }

  expectChildSuccess(pid);
}

#endif
