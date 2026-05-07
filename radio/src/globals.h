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

#pragma once

#include <atomic>

#include "definitions.h"
#include "dataconstants.h"
#include "edgetx_types.h"

PACK(struct GlobalData {
  uint8_t externalAntennaEnabled:1;
  uint8_t authenticationCount:2;
  uint8_t upgradeModulePopup:1;
  uint8_t internalModuleVersionChecked:1;
  uint8_t spare:3;
});

extern GlobalData globalData;

extern uint16_t sessionTimer;
extern uint16_t s_timeCumThr;
extern uint16_t s_timeCum16ThrP;

inline uint16_t getSessionTimer()
{
  return __atomic_load_n(&sessionTimer, __ATOMIC_RELAXED);
}

inline uint16_t addSessionTimer(uint16_t value)
{
  return __atomic_add_fetch(&sessionTimer, value, __ATOMIC_RELAXED);
}

inline uint16_t exchangeSessionTimer(uint16_t value)
{
  return __atomic_exchange_n(&sessionTimer, value, __ATOMIC_RELAXED);
}

inline void setSessionTimer(uint16_t value)
{
  __atomic_store_n(&sessionTimer, value, __ATOMIC_RELAXED);
}

inline uint16_t getThrottleRuntime()
{
  return __atomic_load_n(&s_timeCumThr, __ATOMIC_RELAXED);
}

inline void addThrottleRuntime(uint16_t value)
{
  __atomic_add_fetch(&s_timeCumThr, value, __ATOMIC_RELAXED);
}

inline void setThrottleRuntime(uint16_t value)
{
  __atomic_store_n(&s_timeCumThr, value, __ATOMIC_RELAXED);
}

inline uint16_t getThrottlePercentRuntime()
{
  return __atomic_load_n(&s_timeCum16ThrP, __ATOMIC_RELAXED);
}

inline void addThrottlePercentRuntime(uint16_t value)
{
  __atomic_add_fetch(&s_timeCum16ThrP, value, __ATOMIC_RELAXED);
}

inline void setThrottlePercentRuntime(uint16_t value)
{
  __atomic_store_n(&s_timeCum16ThrP, value, __ATOMIC_RELAXED);
}

#if defined(OVERRIDE_CHANNEL_FUNCTION)
#define OVERRIDE_CHANNEL_UNDEFINED -4096
extern safetych_t safetyCh[MAX_OUTPUT_CHANNELS];

inline safetych_t getSafetyChannel(uint8_t index)
{
  return __atomic_load_n(&safetyCh[index], __ATOMIC_RELAXED);
}

inline void setSafetyChannel(uint8_t index, safetych_t value)
{
  __atomic_store_n(&safetyCh[index], value, __ATOMIC_RELAXED);
}
#endif

extern std::atomic<uint8_t> trimsCheckTimer;
extern std::atomic<uint8_t> trimsDisplayTimer;
extern std::atomic<uint8_t> trimsDisplayMask;
extern uint32_t maxMixerDuration;

inline uint32_t getMaxMixerDuration()
{
  return __atomic_load_n(&maxMixerDuration, __ATOMIC_RELAXED);
}

inline void resetMaxMixerDuration()
{
  __atomic_store_n(&maxMixerDuration, 0, __ATOMIC_RELAXED);
}

inline void updateMaxMixerDuration(uint32_t value)
{
  uint32_t current = getMaxMixerDuration();
  while (value > current &&
         !__atomic_compare_exchange_n(&maxMixerDuration, &current, value, false,
                                      __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
  }
}

#if defined(AUDIO)
extern std::atomic<uint8_t> requiredSpeakerVolume;
#endif

extern std::atomic<uint8_t> requiredBacklightBright;

enum MainRequest {
  REQUEST_SCREENSHOT,
  REQUEST_FLIGHT_RESET,
  REQUEST_MAIN_VIEW,
};

extern uint8_t mainRequestFlags;

PACK(struct MixState {
  uint16_t delay; // max = 2550
  int16_t  now;  // timer trigger source -> off, abs, stk, stk%, sw/!sw, !m_sw/!m_sw
  int16_t  prev;
});

extern MixState mixState[MAX_MIXERS];
extern std::atomic<uint8_t> mixStateActiveExpos[MAX_MIXERS];
extern std::atomic<uint8_t> mixStateActiveMixes[MAX_MIXERS];
extern int32_t act[MAX_MIXERS];

// static variables used in evalFlightModeMixes - moved here so they don't interfere with the stack
// It's also easier to initialize them here.
extern int8_t  virtualInputsTrims[MAX_INPUTS];

inline int8_t getVirtualInputTrim(uint8_t index)
{
  return __atomic_load_n(&virtualInputsTrims[index], __ATOMIC_RELAXED);
}

inline void setVirtualInputTrim(uint8_t index, int8_t value)
{
  __atomic_store_n(&virtualInputsTrims[index], value, __ATOMIC_RELAXED);
}

extern int16_t anas [MAX_INPUTS];
extern int16_t trims[MAX_TRIMS];
extern int32_t chans[MAX_OUTPUT_CHANNELS];
extern int16_t ex_chans[MAX_OUTPUT_CHANNELS]; // Outputs (before LIMITS) of the last perMain
extern int16_t channelOutputs[MAX_OUTPUT_CHANNELS];

inline int16_t getInputValue(uint8_t index)
{
  return __atomic_load_n(&anas[index], __ATOMIC_RELAXED);
}

inline void setInputValue(uint8_t index, int16_t value)
{
  __atomic_store_n(&anas[index], value, __ATOMIC_RELAXED);
}

inline int16_t getRawChannelOutput(uint8_t index)
{
  return __atomic_load_n(&ex_chans[index], __ATOMIC_RELAXED);
}

inline void setRawChannelOutput(uint8_t index, int16_t value)
{
  __atomic_store_n(&ex_chans[index], value, __ATOMIC_RELAXED);
}

inline int16_t getChannelOutput(uint8_t index)
{
  return __atomic_load_n(&channelOutputs[index], __ATOMIC_RELAXED);
}

inline void setChannelOutput(uint8_t index, int16_t value)
{
  __atomic_store_n(&channelOutputs[index], value, __ATOMIC_RELAXED);
}

typedef uint16_t BeepANACenter;
extern BeepANACenter bpanaCenter;

extern uint8_t s_mixer_first_run_done;

extern int16_t calibratedAnalogs[MAX_ANALOG_INPUTS];

inline int16_t getCalibratedAnalog(uint8_t index)
{
  return __atomic_load_n(&calibratedAnalogs[index], __ATOMIC_RELAXED);
}

inline void setCalibratedAnalog(uint8_t index, int16_t value)
{
  __atomic_store_n(&calibratedAnalogs[index], value, __ATOMIC_RELAXED);
}

extern uint8_t g_beepCnt;
extern uint8_t beepAgain;
extern std::atomic<uint16_t> lightOffCounter;
extern std::atomic<uint8_t> flashCounter;
extern uint8_t mixWarning;
