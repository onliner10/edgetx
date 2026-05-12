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

#include "edgetx.h"
#include "timers.h"
#include "switches.h"

Tmr10msCounter g_tmr10ms;

extern "C" tmr10ms_t timersGet10msTick()
{
  return get_tmr10ms();
}

#define MAX_ALERT_TIME   60

#if TIMERS > MAX_TIMERS
#error "Timers cannot exceed " .. MAX_TIMERS
#endif

TimerState timersStates[TIMERS] = { { 0 } };

void timerReset(uint8_t idx)
{
  if (idx >= TIMERS) return;

  TimerState & timerState = timersStates[idx];
  timerState.state = TMR_OFF; // is changed to RUNNING dep from mode
  setTimerStateValue(idx, g_model.timers[idx].start);
  timerState.val_10ms = 0 ;
}

void timerSet(int idx, int val)
{
  if (idx < 0 || idx >= TIMERS) return;

  TimerState & timerState = timersStates[idx];
  timerState.state = TMR_OFF; // is changed to RUNNING dep from mode
  setTimerStateValue(idx, val);
  timerState.val_10ms = 0 ;
}

bool isTimerMinuteBeepDue(const TimerData &timer, tmrval_t announceVal)
{
  if (!timer.minuteBeep || (announceVal % 60) != 0) return false;

  if (timer.minuteBeepStart == 0) return true;

  const tmrval_t threshold = timer.minuteBeepStart * 60;
  if (timer.start && !timer.showElapsed) {
    return announceVal <= threshold;
  }
  return announceVal >= threshold;
}

void restoreTimers()
{
  for (uint8_t i=0; i<TIMERS; i++) {
    if (g_model.timers[i].persistent) {
      setTimerStateValue(i, g_model.timers[i].value);
    }
  }
}

void saveTimers()
{
  for (uint8_t i=0; i<TIMERS; i++) {
    if (g_model.timers[i].persistent) {
      tmrval_t timerValue = getTimerStateValue(i);
      if (g_model.timers[i].value != (uint16_t)timerValue) {
        g_model.timers[i].value = timerValue;
        storageDirty(EE_MODEL);
      }
    }
  }
}

#define THR_TRG_TRESHOLD    13      // approximately 10% full throttle

void evalTimers(int16_t throttle, uint8_t tick10ms)
{

#if defined(SURFACE_RADIO)
  // For surface radio throttle off position is at 0%
  throttle = 2 * abs(throttle - (RESX >> (RESX_SHIFT-6)));
#endif
  for (uint8_t i=0; i<TIMERS; i++) {
    tmrmode_t timerMode = g_model.timers[i].mode;
    tmrstart_t timerStart = g_model.timers[i].start;
    int16_t     timerSwtch = g_model.timers[i].swtch;
    TimerState * timerState = &timersStates[i];
    uint32_t showElapsed = g_model.timers[i].showElapsed;

    if (timerMode) {
      if ((timerState->state == TMR_OFF)
          && (timerMode != TMRMODE_THR_START)
          && (timerMode != TMRMODE_START)) {
       
        timerState->state = TMR_RUNNING;
        timerState->cnt = 0;
        timerState->sum = 0;
      }

      if (timerMode == TMRMODE_THR_REL) {
        timerState->cnt++;
        timerState->sum += throttle;
      }

      if ((timerState->val_10ms += tick10ms) >= 100) {
        tmrval_t currentTimerVal = getTimerStateValue(i);
        if (currentTimerVal == TIMER_MAX) break;
        if (currentTimerVal == TIMER_MIN) break;

        timerState->val_10ms -= 100;
        tmrval_t newTimerVal = currentTimerVal;
        if (timerStart) newTimerVal = timerStart - newTimerVal;

        if (timerMode == TMRMODE_START) {
          // Start timer based on switch
          if (getSwitch(timerSwtch) && timerState->state == TMR_OFF) {
            timerState->state = TMR_RUNNING;  // start timer running
            timerState->cnt = 0;
            timerState->sum = 0;
          }
          if (timerState->state != TMR_OFF) {
            newTimerVal++;
          } 
        } else if (getSwitch(timerSwtch)) {

          // Modes conditional on switch at any time
          if (timerMode == TMRMODE_ON) {
            newTimerVal++;
          } else if (timerMode == TMRMODE_THR) {
            if (throttle) newTimerVal++;
          } else if (timerMode == TMRMODE_THR_REL) {
            // throttle was normalized to 0 to 128 value
            // (throttle/64*2 (because - range is added as well)
            if (divOr(timerState->sum, timerState->cnt, 0) >= 128) {
              newTimerVal++;  // add second used of throttle
              timerState->sum -= 128 * timerState->cnt;
            }
            timerState->cnt = 0;
          } else if (timerMode == TMRMODE_THR_START) {
            // we can't rely on (throttle || newTimerVal > 0) as a detection if
            // timer should be running because having persistent timer brakes
            // this rule
            if ((throttle > THR_TRG_TRESHOLD) && timerState->state == TMR_OFF) {
              timerState->state = TMR_RUNNING;  // start timer running
              timerState->cnt = 0;
              timerState->sum = 0;
              // TRACE("Timer[%d] THr triggered", i);
            }
            if (timerState->state != TMR_OFF) newTimerVal++;
          }
        }

        switch (timerState->state) {
          case TMR_RUNNING:
            if (timerStart && newTimerVal >= (tmrval_t)timerStart) {
              AUDIO_TIMER_ELAPSED(i);
              timerState->state = TMR_NEGATIVE;
              // TRACE("Timer[%d] negative", i);
            }
            break;
          case TMR_NEGATIVE:
            if (newTimerVal >= (tmrval_t)timerStart + MAX_ALERT_TIME) {
              timerState->state = TMR_STOPPED;
              // TRACE("Timer[%d] stopped state at %d", i, newTimerVal);
            }
            break;
        }

        // if counting backwards - display backwards
        if (timerStart) newTimerVal = timerStart - newTimerVal;

        if (newTimerVal != currentTimerVal) {
          setTimerStateValue(i, newTimerVal);
          if (timerState->state == TMR_RUNNING) {
            if (g_model.timers[i].countdownBeep && g_model.timers[i].start) {
              AUDIO_TIMER_COUNTDOWN(i, newTimerVal);
            }
            tmrval_t announceVal = newTimerVal;
            if (showElapsed) announceVal = timerStart - newTimerVal;
            if (isTimerMinuteBeepDue(g_model.timers[i], announceVal)) {
              AUDIO_TIMER_MINUTE(announceVal);
              // TRACE("Timer[%d] %d minute announcement", i, newTimerVal/60);
            }
          }
        }
      }
    }
  }
}

int16_t throttleSource2Source(int16_t thrSrc)
{
  if (thrSrc == 0) {
    return int16_t(MIXSRC_FIRST_STICK + inputMappingGetThrottle());
  }
  if (--thrSrc < MAX_POTS)
    return (int16_t)(thrSrc + MIXSRC_FIRST_POT);
  return (int16_t)(thrSrc - MAX_POTS + MIXSRC_FIRST_CH);
}

int16_t source2ThrottleSource(int16_t src)
{
  if (src == MIXSRC_FIRST_STICK + inputMappingGetThrottle()) {
    return 0;
  } else if (src <= MIXSRC_LAST_POT) {
    return src - MIXSRC_FIRST_POT + 1;
  } else if (src <= MIXSRC_LAST_CH) {
    return src - MIXSRC_FIRST_CH + MAX_POTS + 1;
  }
  return -1;
}
