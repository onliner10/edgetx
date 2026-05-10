/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   libopenui - https://github.com/opentx/libopenui
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

#include "mainwindow.h"

#include "board.h"
#include "debug.h"
#include "edgetx.h"
#include "keyboard_base.h"
#include "lcd.h"
#include "layout.h"
#include "LvglWrapper.h"
#include "etx_lv_theme.h"
#include "view_main.h"

#include "os/sleep.h"
#include "os/time.h"
#include "sdcard.h"

#if defined(SIMU)
#include "targets/simu/simu_ui_automation.h"
#endif

MainWindow* MainWindow::_instance = nullptr;

#if defined(SIMU)
static bool forceBackgroundCanvasCreateFailure = false;
void etxCreateForceObjectAllocationFailureForTest(bool force);

void mainWindowForceBackgroundCanvasCreateFailureForTest(bool force)
{
  forceBackgroundCanvasCreateFailure = force;
}
#endif

static lv_obj_t* createBackgroundCanvas(lv_obj_t* parent)
{
#if defined(SIMU)
  if (forceBackgroundCanvasCreateFailure) return nullptr;
#endif
  return lv_canvas_create(parent);
}

MainWindow* MainWindow::instance()
{
  if (!_instance) _instance = new MainWindow();
  return _instance;
}

MainWindow::MainWindow() : Window(nullptr, {0, 0, LCD_W, LCD_H})
{
  setWindowFlag(OPAQUE);

  withLive([&](LiveWindow& live) {
    auto obj = live.lvobj();
    etx_solid_bg(obj);

    background = createBackgroundCanvas(obj);
    if (background) lv_obj_center(background);
  });
}

void MainWindow::emptyTrash()
{
  for (auto window : trash) {
    delete window;
  }
  trash.clear();
}

uint32_t MainWindow::runMainLoopTick()
{
  return run(NormalUiTick{});
}

uint32_t MainWindow::runActiveLoopTick()
{
  return run(ActiveUiTick{});
}

uint32_t MainWindow::run(NormalUiTick mode)
{
  return runUiTick(mode);
}

uint32_t MainWindow::run(ModalUiTick mode)
{
  return runUiTick(mode);
}

uint32_t MainWindow::run(ActiveUiTick mode)
{
  return runUiTick(mode);
}

void MainWindow::refreshModelWidgets(NormalUiTick)
{
  if (widgetRefreshEnable)
    ViewMain::refreshWidgets(ViewMain::WidgetRefreshToken{});
}

void MainWindow::collectDeletedWindows(NormalUiTick)
{
  emptyTrash();
}

template <class TickMode>
uint32_t MainWindow::runUiTick(TickMode mode)
{
  lcdFlushPoll();
  if (lcdFlushIsBusy()) {
    return 1;
  }

  uint32_t nextRun = LvglWrapper::instance()->run();

#if defined(DEBUG_WINDOWS)
  auto start = time_get_ms();
#endif

  refreshModelWidgets(mode);

  auto opaque = Window::firstOpaque();
  if (opaque) {
    opaque->checkEvents();
  }

  auto copy = children;
  for (auto it = copy.begin(); it != copy.end();) {
    auto child = *it;
    ++it;
    if (child && child->isBubblePopup()) {
      child->checkEvents();
    }
  }

  collectDeletedWindows(mode);

#if defined(SIMU)
  SimuUiAutomation::menuTick();
#endif

#if defined(DEBUG_WINDOWS)
  auto delta = time_get_ms() - start;
  if (delta > 10) {
    TRACE_WINDOWS("MainWindow::run took %dms", delta);
  }
#endif

  return nextRun;
}

void MainWindow::shutdown()
{
  // Called when USB is connected in SD card mode

  // Delete main view screens
  LayoutFactory::deleteCustomScreens();

  // clear layer stack first
  for (Window* w = Window::topWindow(); w; w = Window::topWindow())
    w->deleteLater();

  clear();
  emptyTrash();

  // Re-add background canvas
  background = nullptr;
  withLive([&](LiveWindow& live) {
    auto obj = live.lvobj();
    background = createBackgroundCanvas(obj);
    if (background) lv_obj_center(background);
  });
}

bool MainWindow::setBackgroundImage(std::string& fileName)
{
  if (fileName.empty()) return false;

  // Try to load bitmap.
  auto newBitmap = BitmapBuffer::loadBitmap(fileName.c_str(), BMP_RGB565);

  if (newBitmap) {
    if (!background) {
      delete newBitmap;
      return false;
    }

    auto oldBitmap = backgroundBitmap;
    backgroundBitmap = newBitmap;
    lv_canvas_set_buffer(background, backgroundBitmap->getData(), backgroundBitmap->width(),
                         backgroundBitmap->height(), LV_IMG_CF_TRUE_COLOR);
    if (oldBitmap != nullptr) delete oldBitmap;
    return true;
  }

  return false;
}

#if defined(SIMU)
bool mainWindowBackgroundCanvasCreateFailureLeavesNoCanvasForTest()
{
  class TestMainWindow : public MainWindow
  {
   public:
    TestMainWindow() : MainWindow() {}
    bool hasBackgroundCanvas() const { return background != nullptr; }
  };

  mainWindowForceBackgroundCanvasCreateFailureForTest(true);
  auto window = new TestMainWindow();
  mainWindowForceBackgroundCanvasCreateFailureForTest(false);

  return window && !window->hasBackgroundCanvas();
}

bool mainWindowObjectAllocationFailureFailsClosedForTest()
{
  class TestMainWindow : public MainWindow
  {
   public:
    TestMainWindow() : MainWindow() {}
    bool hasBackgroundCanvas() const { return background != nullptr; }
  };

  etxCreateForceObjectAllocationFailureForTest(true);
  auto window = new TestMainWindow();
  etxCreateForceObjectAllocationFailureForTest(false);

  bool ok = window && window->getLvObjForTest() == nullptr &&
            !window->isAvailable() && !window->isVisible() &&
            !window->hasBackgroundCanvas();
  delete window;
  return ok;
}
#endif

void MainWindow::blockUntilClose(bool checkPwr, std::function<bool(void)> closeCondition, bool isError)
{
  // reset input devices to avoid
  // RELEASED/CLICKED to be called in a loop
  lv_indev_reset(nullptr, nullptr);

  if (isError) {
    // refresh screen and turn backlight on
    backlightEnable(BACKLIGHT_LEVEL_MAX);
    // On startup error wait for power button to be released
    while (pwrPressed()) {
      WDG_RESET();
      run(ModalUiTick{});
      sleep_ms(10);
    }
  }

  while (!closeCondition()) {
    if (checkPwr) {
      auto check = pwrCheck();
      if (check == e_power_off) {
        boardOff();
#if defined(SIMU)
        return;
#endif
      } else if (check == e_power_press) {
        WDG_RESET();
        sleep_ms(1);
        continue;
      }
    }

    resetBacklightTimeout();
    if (isError)
      backlightEnable(BACKLIGHT_LEVEL_MAX);
    else
      checkBacklight();

    WDG_RESET();

    run(ModalUiTick{});
    sleep_ms(10);
  }
}

void MainWindow::blockUntilClosed(Window& window, bool checkPwr, bool isError)
{
  blockUntilClosed(window, checkPwr, nullptr, isError);
}

void MainWindow::blockUntilClosed(
    Window& window, bool checkPwr,
    const std::function<bool(void)>& closeCondition, bool isError)
{
  blockUntilClose(checkPwr, [&]() {
    if (window.deleted()) return true;
    if (closeCondition && closeCondition()) {
      window.deleteLater();
      return true;
    }
    return false;
  }, isError);
}
