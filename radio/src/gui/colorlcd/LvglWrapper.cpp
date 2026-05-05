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

#include "LvglWrapper.h"

#include "edgetx.h"
#include "etx_lv_theme.h"
#include "hal/rotary_encoder.h"
#include "keyboard_base.h"
#include "mainwindow.h"
#include "os/time.h"
#include "view_main.h"

#include <new>

LvglWrapper* LvglWrapper::_instance = nullptr;

static lv_indev_drv_t touchDriver;
static lv_indev_drv_t keyboard_drv;
#if defined(ROTARY_ENCODER_NAVIGATION)
static lv_indev_drv_t rotaryDriver;

static lv_indev_t* rotaryDevice = nullptr;
#endif
static lv_indev_t* keyboardDevice = nullptr;
static lv_indev_t* touchDevice = nullptr;

static constexpr uint32_t TOUCH_INDEV_READ_PERIOD_MS = 10;
static constexpr uint32_t TOUCH_DISPLAY_REFRESH_PERIOD_MS = 20;
static constexpr uint32_t UI_INPUT_ACTIVE_PERIOD_MS = 80;
static constexpr uint8_t TOUCH_SCROLL_LIMIT_PX = 4;
static constexpr uint8_t TOUCH_SCROLL_THROW = 3;

static bool touchPressed = false;
static uint32_t touchActiveUntil = 0;
static uint32_t keyboardActiveUntil = 0;
static uint32_t rotaryActiveUntil = 0;

#if defined(ROTARY_ENCODER_NAVIGATION)
static bool rotaryObserved = false;
static rotenc_t observedRotaryValue = 0;
#endif

#if defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
LvglAdaptiveUiPumpStats lvglAdaptiveUiPumpStats = {};

void lvglAdaptiveUiPumpRecordFlush()
{
  lvglAdaptiveUiPumpStats.flushCount++;
}

void lvglAdaptiveUiPumpRecordVblankWait(uint32_t durationMs)
{
  lvglAdaptiveUiPumpStats.vblankWaitCount++;
  lvglAdaptiveUiPumpStats.vblankWaitMs += durationMs;
  if (durationMs > lvglAdaptiveUiPumpStats.vblankWaitMaxMs) {
    lvglAdaptiveUiPumpStats.vblankWaitMaxMs = durationMs;
  }
}
#endif

static bool time_reached(uint32_t now, uint32_t target)
{
  return (int32_t)(now - target) >= 0;
}

static void mark_active_until(uint32_t& activeUntil)
{
  activeUntil = time_get_ms() + UI_INPUT_ACTIVE_PERIOD_MS;
}

static bool active_until_pending(uint32_t now, uint32_t activeUntil)
{
  return !time_reached(now, activeUntil);
}

static void reset_inactivity()
{
  inactivity.counter = 0;
  if (g_eeGeneral.backlightMode & e_backlight_mode_keys) {     
    resetBacklightTimeout();
  }
}

static lv_obj_t* get_focus_obj(lv_indev_t* indev)
{
  lv_group_t * g = indev->group;
  if(g == nullptr) return nullptr;
  return lv_group_get_focused(g);
}

static lv_indev_data_t kb_data_backup;

static void backup_kb_data(lv_indev_data_t* data)
{
  memcpy(&kb_data_backup, data, sizeof(lv_indev_data_t));
}

static void copy_kb_data_backup(lv_indev_data_t* data)
{
  memcpy(data, &kb_data_backup, sizeof(lv_indev_data_t));
}

static bool evt_to_indev_data(event_t evt, lv_indev_data_t *data)
{
  event_t key = EVT_KEY_MASK(evt);

  switch(key) {

  case KEY_ENTER:
    data->key = LV_KEY_ENTER;
    break;

  case KEY_EXIT:
    if (evt == EVT_KEY_BREAK(KEY_EXIT)) {
      data->key = LV_KEY_ESC;
      data->state = LV_INDEV_STATE_PRESSED;
      return true;
    }
    return false;

  default:
    // abort LVGL event
    return false;
  }

  event_t flgs = evt & _MSK_KEY_FLAGS;
  if (flgs != _MSK_KEY_BREAK && flgs != _MSK_KEY_LONG_BRK) {
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }

  return true;
}

static void dispatch_kb_event(Window* w, event_t evt)
{
  if (w) w->dispatchKeyboardEvent(evt);
}

static void keyboardDriverRead(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
  data->key = 0;

  if (isEvent()) { // event waiting
    mark_active_until(keyboardActiveUntil);

    event_t evt = getEvent();

    if ((evt & _MSK_KEY_FLAGS) == _MSK_KEY_LONG_BRK) {
      data->state = LV_INDEV_STATE_RELEASED;
      backup_kb_data(data);
      return;
    }

    if(evt == EVT_KEY_FIRST(KEY_PAGEUP) ||
       evt == EVT_KEY_FIRST(KEY_PAGEDN) ||
       evt == EVT_KEY_FIRST(KEY_ENTER) ||
       evt == EVT_KEY_FIRST(KEY_MODEL) ||
       evt == EVT_KEY_FIRST(KEY_EXIT) ||
       evt == EVT_KEY_FIRST(KEY_TELE) ||
       evt == EVT_KEY_FIRST(KEY_SYS)) {
      // generate acoustic/haptic feedback if radio settings allow
      audioKeyPress();
    }

    // no focused item ?
    auto obj = get_focus_obj(keyboardDevice);
    if (!obj) {
      auto w = Window::topWindow();
      dispatch_kb_event(w, evt);
      backup_kb_data(data);
      return;
    }

    // not an LVGL key ?
    bool is_lvgl_evt = evt_to_indev_data(evt, data);
    if (!is_lvgl_evt) {
      auto w = Window::fromLvObj(obj);
      // If no window, check for keyboard window
      if (!w) w = Keyboard::keyboardWindow();
      dispatch_kb_event(w, evt);
      return;
    }

    backup_kb_data(data);
    return;
  }

  // no event: send a copy of the last one
  copy_kb_data_backup(data);

  // simulate EXIT release: necessary to map
  // EVT_KEY_BREAK(KEY_EXIT) to LV_EVENT_CANCEL
  if (data->key == LV_KEY_ESC && data->state == LV_INDEV_STATE_PRESSED) {
    data->state = LV_INDEV_STATE_RELEASED;
    backup_kb_data(data);
  }
}

extern "C" void touchDriverRead(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
#if defined(HARDWARE_TOUCH)
  static lv_indev_data_t touch_data_backup;

  TouchReadResult touch = touchPanelRead();
  if (touch.isCancel()) {
    data->state = LV_INDEV_STATE_RELEASED;
    touchPressed = false;
    lv_indev_reset(touchDevice, nullptr);
    memcpy(&touch_data_backup, data, sizeof(lv_indev_data_t));
    return;
  }
  const TouchState* touchState = touch.event();
  if (!touchState) {
    memcpy(data, &touch_data_backup, sizeof(lv_indev_data_t));
    return;
  }

  TouchState st = *touchState;

  // no touch input if backlight is disabled
  if (!isBacklightEnabled()) {
    reset_inactivity();
    // assume press and wait for release
    data->state = LV_INDEV_STATE_PRESSED;
    touchPressed = false;
    lv_indev_wait_release(touchDevice);
    data->state = LV_INDEV_STATE_RELEASED;
    memcpy(&touch_data_backup, data, sizeof(lv_indev_data_t));
    return;
  }

  // no touch input if special function is used
  if (isFunctionActive(FUNCTION_DISABLE_TOUCH)) {
    data->state = LV_INDEV_STATE_RELEASED;
    touchPressed = false;
    lv_indev_reset(touchDevice, nullptr);
    memcpy(&touch_data_backup, data, sizeof(lv_indev_data_t));
    return;
  }
  
  if(st.event == TE_NONE) {
    TRACE("TE_NONE");
  } else {
    if(st.event == TE_DOWN || st.event == TE_SLIDE) {
      TRACE("TE_PRESSED");
      data->state = LV_INDEV_STATE_PRESSED;
      touchPressed = true;
      mark_active_until(touchActiveUntil);
    } else {
      TRACE("TE_RELEASED");
      data->state = LV_INDEV_STATE_RELEASED;
      touchPressed = false;
      mark_active_until(touchActiveUntil);
    }
    data->point.x = st.x;
    data->point.y = st.y;
  }

  static bool onebeep=true; // TODO... This probably needs to be fixed in the driver it's sending two events
  if (st.event == TE_DOWN) { // on first touch (same logic as key down)
      reset_inactivity();    // reset activity counter
      if(onebeep)
        audioKeyPress();       // provide acoustic and/or haptic feedback if requested in settings
      onebeep = false;
  } else {
    onebeep = true;
  }

  memcpy(&touch_data_backup, data, sizeof(lv_indev_data_t));
#endif
}

#if defined(USE_HATS_AS_KEYS)

int16_t getEmuRotaryData();

static void rotaryDriverRead(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
  int16_t diff = getEmuRotaryData();

  if(diff != 0) {
    mark_active_until(rotaryActiveUntil);
    reset_inactivity();
    audioKeyPress();

    data->enc_diff = diff;
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// libopenui_depends.h
int8_t rotaryEncoderGetAccel() { return 0; }

#elif defined(ROTARY_ENCODER_NAVIGATION)
extern volatile uint32_t rotencDt;
static int8_t _rotary_enc_accel = 0;

static void rotaryDriverRead(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
  static rotenc_t prevPos = 0;
  static int8_t prevDir = 0;
  static uint32_t lastDt = 0;

  rotenc_t newPos = rotaryEncoderGetValue();
  rotenc_t diff = newPos - prevPos;

  data->enc_diff = (int16_t)diff;
  data->state = LV_INDEV_STATE_RELEASED;

  if (diff != 0) {
    mark_active_until(rotaryActiveUntil);
    prevPos = newPos;
    reset_inactivity();

    int8_t dir = 0;
    if (diff < 0) dir = -1;
    else if (diff > 0) dir = 1;

    if (dir == prevDir) {
      auto dt = rotencDt - lastDt;
      dt = max(dt, (uint32_t)1);

      auto dx_dt = (diff * diff * 50) / dt;
      dx_dt = min(dx_dt, (uint32_t)100);

      _rotary_enc_accel = (int8_t)dx_dt;
    } else {
      _rotary_enc_accel = 0;
    }

    prevDir = dir;
    lastDt = rotencDt;
  }
}

int8_t rotaryEncoderGetAccel() { return _rotary_enc_accel; }

#else // !defined(ROTARY_ENCODER_NAVIGATION)

int8_t rotaryEncoderGetAccel() { return 0; }

#endif // defined(ROTARY_ENCODER_NAVIGATION)

static void init_lvgl_drivers()
{
  // Register the driver and save the created display object
  lcdInitDisplayDriver();

  auto refrTimer = _lv_disp_get_refr_timer(nullptr);
  if (refrTimer) {
    lv_timer_set_period(refrTimer, TOUCH_DISPLAY_REFRESH_PERIOD_MS);
  }
 
  // Register the driver in LVGL and save the created input device object
  lv_indev_drv_init(&touchDriver);          /*Basic initialization*/
  touchDriver.type = LV_INDEV_TYPE_POINTER; /*See below.*/
  touchDriver.read_cb = touchDriverRead;      /*See below.*/
  touchDriver.scroll_limit = TOUCH_SCROLL_LIMIT_PX;
  touchDriver.scroll_throw = TOUCH_SCROLL_THROW;
  touchDevice = lv_indev_drv_register(&touchDriver);
  if (touchDriver.read_timer) {
    lv_timer_set_period(touchDriver.read_timer, TOUCH_INDEV_READ_PERIOD_MS);
  }

#if defined(ROTARY_ENCODER_NAVIGATION)
  lv_indev_drv_init(&rotaryDriver);
  rotaryDriver.type = LV_INDEV_TYPE_ENCODER;
  rotaryDriver.read_cb = rotaryDriverRead;
  rotaryDevice = lv_indev_drv_register(&rotaryDriver);
#endif

  lv_indev_drv_init(&keyboard_drv);
  keyboard_drv.type = LV_INDEV_TYPE_KEYPAD;
  keyboard_drv.read_cb = keyboardDriverRead;
  keyboardDevice = lv_indev_drv_register(&keyboard_drv);
}

LvglWrapper::LvglWrapper()
{
  init_lvgl_drivers();

  static lv_theme_t theme;

  /* Initialize default theme */
  theme.disp = NULL;
  theme.color_primary = lv_palette_main(LV_PALETTE_BLUE);
  theme.color_secondary = lv_palette_main(LV_PALETTE_RED);
  theme.font_small = LV_FONT_DEFAULT;
  theme.font_normal = LV_FONT_DEFAULT;
  theme.font_large = LV_FONT_DEFAULT;
  theme.flags = 0;

  /* Assign the theme to the current display*/
  lv_disp_set_theme(NULL, &theme);

  // Integrate STB image handling into lvgl
  extern void lv_stb_init();
  lv_stb_init();

  // Create main window and load that screen
  auto window = MainWindow::instance();
  lv_scr_load(window->getLvObj());

  // create ViewMain window
  ViewMain::instance();
}

LvglWrapper* LvglWrapper::instance()
{
  if (!_instance) _instance = new (std::nothrow) LvglWrapper();
  return _instance;
}

uint32_t LvglWrapper::run()
{
  // Detect nested calls
  static bool updating = false;

  if (!updating) {
    // Normal UI loop - call lgvl timer handler
    updating = true;
#if defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
    uint32_t start = time_get_ms();
#endif
    uint32_t nextRun = lv_timer_handler();
#if defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
    uint32_t duration = time_get_ms() - start;
    lvglAdaptiveUiPumpStats.handlerCount++;
    lvglAdaptiveUiPumpStats.handlerDurationMs += duration;
    if (duration > lvglAdaptiveUiPumpStats.handlerMaxDurationMs) {
      lvglAdaptiveUiPumpStats.handlerMaxDurationMs = duration;
    }
#endif
    if (nextRun == LV_NO_TIMER_READY) {
      nextRunKnown = false;
    } else {
      nextRunKnown = true;
      nextRunAt = time_get_ms() + nextRun;
    }
    updating = false;
    return nextRun;
  } else {
    // Used when running the loop manually from within
    // the LVGL timer handler (blocking UI code)
    lv_refr_now(nullptr);

    lv_indev_t* indev = nullptr;
    while((indev = lv_indev_get_next(indev)) != nullptr) {
      lv_indev_read_timer_cb(indev->driver->read_timer);
    }

    return 1;
  }
}

bool LvglWrapper::getNextRunDelay(uint32_t& delay) const
{
  if (!nextRunKnown) return false;

  uint32_t now = time_get_ms();
  if (time_reached(now, nextRunAt)) {
    delay = 0;
    return true;
  }

  delay = nextRunAt - now;
  return true;
}

bool LvglWrapper::hasAdaptiveWork() const
{
  uint32_t now = time_get_ms();

  if (isEvent() || active_until_pending(now, keyboardActiveUntil)) return true;

#if defined(HARDWARE_TOUCH)
  if (touchPressed || active_until_pending(now, touchActiveUntil)) return true;
#endif

#if defined(ROTARY_ENCODER_NAVIGATION)
  rotenc_t rotaryValue = rotaryEncoderGetValue();
  if (!rotaryObserved) {
    observedRotaryValue = rotaryValue;
    rotaryObserved = true;
  } else if (rotaryValue != observedRotaryValue) {
    observedRotaryValue = rotaryValue;
    mark_active_until(rotaryActiveUntil);
    return true;
  }
  if (active_until_pending(now, rotaryActiveUntil)) return true;
#endif

  if (lv_anim_count_running() > 0) return true;

  lv_disp_t* disp = lv_disp_get_default();
  if (disp && (disp->rendering_in_progress || disp->inv_p > 0)) return true;

  lv_indev_t* indev = nullptr;
  while((indev = lv_indev_get_next(indev)) != nullptr) {
    if (lv_indev_get_scroll_obj(indev) != nullptr) return true;
    if (lv_indev_get_scroll_dir(indev) != LV_DIR_NONE) return true;
  }

  return false;
}

void initLvgl()
{
  LvglWrapper::instance();
}
