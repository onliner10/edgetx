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

#include "startup_shutdown.h"

#include "edgetx.h"
#include "hal/abnormal_reboot.h"
#include "inactivity_timer.h"
#include "mainwindow.h"
#include "os/sleep.h"
#include "stamp.h"
#include "theme_manager.h"

#include <new>

extern void checkSpeakerVolume();

constexpr const char* strip_leading_hyphen(const char* str) {
    return (str[0] == '-') ? str + 1 : str;
}

#if defined(VERSION_TAG)
const std::string ver_str = "" VERSION_TAG;
const std::string nam_str = "" CODENAME;
#else
const std::string ver_str = "" VERSION_PREFIX VERSION;
const std::string nam_str = strip_leading_hyphen("" VERSION_SUFFIX);
const std::string git_str = "(" GIT_STR ")";
#endif

alignas(LZ4Bitmap) const uint8_t __bmp_splash_logo[] __FLASH = {
#include "bmp_logo_edgetx_splash.lbm"
};

static Window* splashScreen = nullptr;

static Window* createFullscreenWindow()
{
  return Window::makeLive<Window>(MainWindow::instance(),
                                  rect_t{0, 0, LCD_W, LCD_H});
}

static Window* createOpaqueFullscreenWindow(LcdColorIndex bgColor)
{
  auto window = createFullscreenWindow();
  if (!window) return nullptr;

  window->setWindowFlag(OPAQUE);
  window->solidBg(bgColor);
  return window;
}

void drawSplash()
{
  if (!sdMounted()) sdInit();

  splashScreen = createFullscreenWindow();
  if (!splashScreen) return;
  splashScreen->withLive([](Window::LiveWindow& live) {
    lv_obj_set_parent(live.lvobj(), lv_layer_top());
  });

  splashScreen->solidBg(COLOR_BLACK_INDEX);

  auto bg = new (std::nothrow) StaticImage(splashScreen, {0, 0, LCD_W, LCD_H},
                            BITMAPS_PATH "/" SPLASH_FILE);
  if (bg) bg->show(bg->hasImage());

  if (!bg || !bg->hasImage()) {
    auto logo =
        static_cast<const LZ4Bitmap*>(static_cast<const void*>(__bmp_splash_logo));
    coord_t x = (LANDSCAPE ? LCD_W / 3 : LCD_W / 2) - logo->width / 2;
    coord_t y = (LANDSCAPE ? LCD_H / 2 : LCD_H * 2 / 5) - logo->height / 2;
    new (std::nothrow) StaticLZ4Image(splashScreen, x, y, logo);

    coord_t w = LAYOUT_SCALE(200);
    x = (LANDSCAPE ? LCD_W * 4 / 5 : LCD_W / 2) - w / 2;
    y = LCD_H - EdgeTxStyles::STD_FONT_HEIGHT * 4;
    new (std::nothrow) StaticText(splashScreen, {x, y, w, EdgeTxStyles::STD_FONT_HEIGHT}, ver_str.c_str(), COLOR_GREY_INDEX, CENTERED);
    new (std::nothrow) StaticText(splashScreen, {x, y + EdgeTxStyles::STD_FONT_HEIGHT, w, EdgeTxStyles::STD_FONT_HEIGHT},
                   nam_str.c_str(), COLOR_GREY_INDEX, CENTERED);
#if !defined(VERSION_TAG)
    new (std::nothrow) StaticText(splashScreen, {x, y + EdgeTxStyles::STD_FONT_HEIGHT * 2, w, EdgeTxStyles::STD_FONT_HEIGHT},
                   git_str.c_str(), COLOR_GREY_INDEX, CENTERED);
#endif
  }

  // Refresh to show splash screen
  MainWindow::instance()->runMainLoopTick();
}

static tmr10ms_t splashStartTime = 0;

void startSplash()
{
  if (!UNEXPECTED_SHUTDOWN()) {
    splashStartTime = get_tmr10ms();
    drawSplash();
  }
}

void cancelSplash()
{
  if (splashScreen) {
    splashScreen->deleteLater();
    splashScreen = nullptr;
    splashStartTime = 0;
  }
}

void waitSplash()
{
  // Handle color splash screen
  if (splashStartTime) {
    inactivityCheckInputs();
    splashStartTime += SPLASH_TIMEOUT;

    MainWindow::instance()->blockUntilClose(true, [=]() {
      if (splashStartTime < get_tmr10ms())
        return true;
      auto evt = getEvent();
      if (evt || inactivityCheckInputs()) {
        if (evt) killEvents(evt);
        return true;
      }
      return false;
    });

    // Reset timer so special/global functions set to !1x don't get triggered
    START_SILENCE_PERIOD();
  }

  cancelSplash();
}

static LAYOUT_VAL_SCALED(SHUTDOWN_CIRCLE_RADIUS, 75)

const int8_t bmp_shutdown_xo[] = {0, 0, -SHUTDOWN_CIRCLE_RADIUS,
                                  -SHUTDOWN_CIRCLE_RADIUS};
const int8_t bmp_shutdown_yo[] = {-SHUTDOWN_CIRCLE_RADIUS, 0, 0,
                                  -SHUTDOWN_CIRCLE_RADIUS};

static Window* shutdownWindow = nullptr;
static StaticIcon* shutdownAnim[4] = {nullptr};
static BitmapBuffer* shutdownSplashImg = nullptr;
static lv_obj_t* shutdownCanvas = nullptr;

#if defined(SIMU)
static bool forceShutdownCanvasCreateFailure = false;
void etxCreateForceObjectAllocationFailureForTest(bool force);

void startupShutdownForceCanvasCreateFailureForTest(bool force)
{
  forceShutdownCanvasCreateFailure = force;
}
#endif

static lv_obj_t* createShutdownCanvas(lv_obj_t* parent)
{
#if defined(SIMU)
  if (forceShutdownCanvasCreateFailure) return nullptr;
#endif
  return lv_canvas_create(parent);
}

void drawSleepBitmap()
{
  if (shutdownWindow) {
    shutdownWindow->clear();
  } else {
    shutdownWindow = createOpaqueFullscreenWindow(COLOR_THEME_PRIMARY1_INDEX);
    if (!shutdownWindow) return;
  }

  if (auto icon = new (std::nothrow) StaticIcon(shutdownWindow, 0, 0, ICON_SHUTDOWN, COLOR_THEME_PRIMARY2_INDEX))
    icon->center(LCD_W, LCD_H);

  // Force screen refresh
  lv_refr_now(nullptr);
}

void cancelShutdownAnimation()
{
  if (shutdownWindow) {
    shutdownWindow->deleteLater();
    shutdownWindow = nullptr;
    shutdownCanvas = nullptr;
    for (int i = 0; i < 4; i += 1) shutdownAnim[i] = nullptr;
  }
}

void drawShutdownAnimation(uint32_t duration, uint32_t totalDuration,
                           const char* message)
{
  if (totalDuration == 0) return;

  if (shutdownWindow == nullptr) {
    shutdownWindow = createOpaqueFullscreenWindow(COLOR_THEME_PRIMARY1_INDEX);
    if (!shutdownWindow) return;

    if (sdMounted() && !shutdownSplashImg)
      shutdownSplashImg = BitmapBuffer::loadBitmap(
          BITMAPS_PATH "/" SHUTDOWN_SPLASH_FILE, BMP_RGB565);

    if (shutdownSplashImg) {
      shutdownWindow->withLive([&](Window::LiveWindow& live) {
        shutdownCanvas = createShutdownCanvas(live.lvobj());
      });
      if (shutdownCanvas) {
        lv_obj_center(shutdownCanvas);
        lv_canvas_set_buffer(shutdownCanvas, shutdownSplashImg->getData(),
                             shutdownSplashImg->width(),
                             shutdownSplashImg->height(), LV_IMG_CF_TRUE_COLOR);
      }
    }
    if (auto icon = new (std::nothrow) StaticIcon(
            shutdownWindow, 0, 0, ICON_SHUTDOWN, COLOR_THEME_PRIMARY2_INDEX))
      icon->center(LCD_W, LCD_H);

    for (int i = 0; i < 4; i += 1) {
      shutdownAnim[i] = new (std::nothrow) StaticIcon(
          shutdownWindow, LCD_W / 2 + bmp_shutdown_xo[i],
          LCD_H / 2 + bmp_shutdown_yo[i],
          (EdgeTxIcon)(ICON_SHUTDOWN_CIRCLE0 + i), COLOR_THEME_PRIMARY2_INDEX);
    }
  }

  int quarter = 4 - (duration * 5) / totalDuration;
  if (quarter < 0) quarter = 0;
  for (int i = 3; i >= quarter; i -= 1) {
    if (shutdownAnim[i]) shutdownAnim[i]->hide();
  }

  MainWindow::instance()->runMainLoopTick();
}

#if defined(SIMU)
bool startupShutdownCanvasCreateFailureLeavesNoCanvasForTest()
{
  static pixel_t pixel = 0;

  cancelShutdownAnimation();
  delete shutdownSplashImg;
  shutdownSplashImg = new BitmapBuffer(BMP_RGB565, 1, 1, &pixel);

  startupShutdownForceCanvasCreateFailureForTest(true);
  drawShutdownAnimation(0, 1, nullptr);
  startupShutdownForceCanvasCreateFailureForTest(false);

  bool result = shutdownWindow && shutdownCanvas == nullptr;
  cancelShutdownAnimation();
  delete shutdownSplashImg;
  shutdownSplashImg = nullptr;

  return result;
}

bool startupShutdownWindowAllocationFailureDoesNotCacheDeadWindowForTest()
{
  MainWindow::instance();
  cancelShutdownAnimation();

  etxCreateForceObjectAllocationFailureForTest(true);
  drawShutdownAnimation(0, 1, nullptr);
  etxCreateForceObjectAllocationFailureForTest(false);

  bool result = shutdownWindow == nullptr && shutdownCanvas == nullptr;
  cancelShutdownAnimation();
  return result;
}

bool fatalErrorScreenAllocationFailureReturnsMissingHandleForTest()
{
  MainWindow::instance();

  etxCreateForceObjectAllocationFailureForTest(true);
  auto screen = drawFatalErrorScreen("fatal");
  etxCreateForceObjectAllocationFailureForTest(false);

  bool visited = false;
  const bool withScreen =
      screen.with([&](Window&) { visited = true; });
  return !screen.isPresentForTest() && !withScreen && !visited;
}
#endif

OptionalWindow<Window> drawFatalErrorScreen(const char* message)
{
  OptionalWindow<Window> screen;
  auto w = createOpaqueFullscreenWindow(COLOR_BLACK_INDEX);
  if (w) {
    screen.reset(w);
    new (std::nothrow) StaticText(
        w,
        rect_t{0, LCD_H / 2 - EdgeTxStyles::STD_FONT_HEIGHT, LCD_W,
               EdgeTxStyles::STD_FONT_HEIGHT * 2},
        message, COLOR_WHITE_INDEX, FONT(XL) | CENTERED);
  }

  return screen;
}

void runFatalErrorScreen(const char* message)
{
  drawFatalErrorScreen(message);

  MainWindow::instance()->blockUntilClose(true, []() {
    return false;
  }, true);
}
