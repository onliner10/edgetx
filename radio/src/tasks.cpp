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

#include "debug.h"
#include "edgetx.h"
#include "os/sleep.h"
#include "os/task.h"
#include "os/time.h"
#include "os/timer.h"
#include "timers_driver.h"
#include "hal/abnormal_reboot.h"
#include "hal/watchdog_driver.h"
#include "telemetry/telemetry.h"

#include "tasks.h"
#include "tasks/mixer_task.h"

#if defined(SIMU)
#include "targets/simu/simulib.h"
#if defined(COLORLCD)
#include "targets/simu/simu_ui_automation.h"
#endif
#endif

#if defined(COLORLCD)
#if defined(SIMU)
#include "lvgl/lvgl.h"
#endif
#if defined(FREE_RTOS)
#include "LvglWrapper.h"
#include "mainwindow.h"
#endif
#include "lcd.h"
#include "startup_shutdown.h"
#endif

task_handle_t menusTaskId;
TASK_DEFINE_STACK(menusStack, MENUS_STACK_SIZE);

#if defined(AUDIO)
task_handle_t audioTaskId;
TASK_DEFINE_STACK(audioStack, AUDIO_STACK_SIZE);
#endif

mutex_handle_t audioMutex;

#define MENU_TASK_PERIOD (50)  // 50ms

#if defined(COLORLCD) && defined(FREE_RTOS)
static constexpr uint32_t LVGL_ADAPTIVE_PUMP_MIN_SLEEP_MS = 1;
static constexpr uint32_t LVGL_ADAPTIVE_PUMP_MAX_SLEEP_MS = 10;

static uint32_t clampLvglPumpSleep(uint32_t sleepTime, uint32_t remaining)
{
  if (sleepTime < LVGL_ADAPTIVE_PUMP_MIN_SLEEP_MS) {
    sleepTime = LVGL_ADAPTIVE_PUMP_MIN_SLEEP_MS;
  } else if (sleepTime > LVGL_ADAPTIVE_PUMP_MAX_SLEEP_MS) {
    sleepTime = LVGL_ADAPTIVE_PUMP_MAX_SLEEP_MS;
  }

  if (sleepTime > remaining) {
    sleepTime = remaining;
  }

  return sleepTime;
}

static void pumpAdaptiveLvglUntilMenuDeadline(uint32_t cycleStart)
{
  while (time_get_ms() - cycleStart < MENU_TASK_PERIOD) {
    lcdFlushPoll();

    if (lcdFlushIsBusy()) {
      uint32_t elapsed = time_get_ms() - cycleStart;
      if (elapsed >= MENU_TASK_PERIOD) break;
      sleep_ms(1);
      continue;
    }

    LvglWrapper* lvgl = LvglWrapper::instance();
    uint32_t nextRun = LVGL_ADAPTIVE_PUMP_MAX_SLEEP_MS;
    bool nextRunKnown = lvgl->getNextRunDelay(nextRun);

    if ((nextRunKnown && nextRun == 0) || lvgl->hasAdaptiveWork()) {
      nextRun = MainWindow::instance()->runActiveLoopTick();
    }

    uint32_t elapsed = time_get_ms() - cycleStart;
    if (elapsed >= MENU_TASK_PERIOD) break;

    uint32_t remaining = MENU_TASK_PERIOD - elapsed;
    sleep_ms(clampLvglPumpSleep(nextRun, remaining));
  }

#if defined(LVGL_ADAPTIVE_UI_PUMP_STATS)
  uint32_t cycleMs = time_get_ms() - cycleStart;
  if (cycleMs > lvglAdaptiveUiPumpStats.menuCycleMaxMs) {
    lvglAdaptiveUiPumpStats.menuCycleMaxMs = cycleMs;
  }
#endif
}
#endif

#if defined(COLORLCD) && defined(CLI)
bool perMainEnabled = true;
#endif

static void menusTask()
{
  mixerTaskInit();
  edgeTxInit();

#if defined(SIMU)
  simuStartupComplete();
#endif

#if defined(COLORLCD) && defined(RTC_BACKUP_RAM)
  if (UNEXPECTED_SHUTDOWN())
    drawFatalErrorScreen(STR_EMERGENCY_MODE);
#endif

#if defined(PWR_BUTTON_PRESS)
  while (task_running()) {
    uint32_t pwr_check = pwrCheck();
    if (pwr_check == e_power_off) {
      break;
    } else if (pwr_check == e_power_press) {
      sleep_ms(MENU_TASK_PERIOD);
      continue;
    }
#else
  while (pwrCheck() != e_power_off) {
#endif
#if defined(COLORLCD) && defined(FREE_RTOS)
    uint32_t menuCycleStart = time_get_ms();
    bool runAdaptiveLvglPump = true;
#else
    time_point_t next_tick = time_point_now();
#endif
    DEBUG_TIMER_START(debugTimerPerMain);
#if defined(COLORLCD) && defined(CLI)
    if (perMainEnabled) {
      perMain();
    }
#if defined(COLORLCD) && defined(FREE_RTOS)
    else {
      runAdaptiveLvglPump = false;
    }
#endif
#else
    perMain();
#endif
    DEBUG_TIMER_STOP(debugTimerPerMain);

#if defined(COLORLCD) && defined(FREE_RTOS)
#if defined(RTC_BACKUP_RAM)
    if (UNEXPECTED_SHUTDOWN()) {
      runAdaptiveLvglPump = false;
    }
#endif
    if (runAdaptiveLvglPump)
      pumpAdaptiveLvglUntilMenuDeadline(menuCycleStart);
    else
      sleep_ms(MENU_TASK_PERIOD);
#else
    sleep_until(&next_tick, MENU_TASK_PERIOD);
#endif
#if defined(SIMU) && defined(COLORLCD)
    SimuUiAutomation::menuTick();
#endif
    resetForcePowerOffRequest();
  }

#if defined(PCBX9E)
  toplcdOff();
#endif

  drawSleepBitmap();
  edgeTxClose();
  boardOff();
}

static void audioTask()
{
  while (!audioQueue.started()) {
    sleep_ms(1);
  }

#if defined(PCBX12S) || defined(RADIO_TX16S) || defined(RADIO_F16) || defined(RADIO_V16)
  // The audio amp needs ~2s to start
  sleep_ms(1000); // 1s
#endif

  time_point_t next_tick = time_point_now();
  while (task_running()) {
    DEBUG_TIMER_SAMPLE(debugTimerAudioIterval);
    DEBUG_TIMER_START(debugTimerAudioDuration);
    audioQueue.wakeup();
    DEBUG_TIMER_STOP(debugTimerAudioDuration);
    sleep_until(&next_tick, 4);
  }
}

static timer_handle_t _timer10ms = TIMER_INITIALIZER;

static void _timer_10ms_cb(timer_handle_t* h)
{
  per10ms();
}

void timer10msStart()
{
  if (!timer_is_created(&_timer10ms)) {
    timer_create(&_timer10ms, _timer_10ms_cb, "10ms", 10, true);
  }

  timer_start(&_timer10ms);
}

#if defined(COLORLCD) && defined(SIMU) && !LV_TICK_CUSTOM
static timer_handle_t _timer1ms = TIMER_INITIALIZER;

static void _timer_1ms_cb(timer_handle_t* h)
{
  // Increment LVGL animation timer
  lv_tick_inc(1);
}

static void timer1msStart()
{
  if (!timer_is_created(&_timer1ms)) {
    timer_create(&_timer1ms, _timer_1ms_cb, "1ms", 1, true);
  }

  timer_start(&_timer1ms);
}
#endif

void tasksStart()
{
  mutex_create(&audioMutex);
  telemetryInit();

#if defined(CLI) && !defined(SIMU)
  cliStart();
#endif

#if defined(COLORLCD) && defined(SIMU) && !LV_TICK_CUSTOM
  timer1msStart();
#endif

#if !defined(SIMU)
  timer10msStart();
#endif

  task_create(&menusTaskId, menusTask, "menus", menusStack, MENUS_STACK_SIZE,
              MENUS_TASK_PRIO);

#if defined(AUDIO)
  task_create(&audioTaskId, audioTask, "audio", audioStack, AUDIO_STACK_SIZE,
              AUDIO_TASK_PRIO);
#endif

  RTOS_START();
}
