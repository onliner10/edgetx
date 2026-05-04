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

#include <climits>

#include "ppm.h"
#include "hal/module_port.h"
#include "mixer_scheduler.h"

#include "edgetx.h"

#include "telemetry/mlink.h"

// Minimum space after the last PPM pulse in us
#define PPM_SAFE_MARGIN 3000 // 3ms

static uint8_t getLastPpmChannel(uint8_t firstCh, int8_t channelsCount)
{
  int16_t lastCh = static_cast<int16_t>(firstCh) + 8 + channelsCount;
  if (lastCh < firstCh) {
    return firstCh;
  }

  return static_cast<uint8_t>(min<int16_t>(MAX_OUTPUT_CHANNELS, lastCh));
}

template <class T>
uint16_t setupPulsesPPM(T*& data, uint8_t channelsStart, int8_t channelsCount)
{
  uint16_t total = 0;
  int16_t PPM_range = g_model.extendedLimits ?
    // range of 0.7 .. 1.7msec
    (512*LIMIT_EXT_PERCENT/100) * 2 : 512 * 2;

  // Total frame length = 22.5msec
  // each pulse is 0.7..1.7ms long with a 0.3ms stop tail
  // The pulse ISR is 2mhz that's why everything is multiplied by 2

  uint8_t firstCh = channelsStart;
  uint8_t lastCh = getLastPpmChannel(firstCh, channelsCount);

  for (uint32_t i = firstCh; i < lastCh; i++) {
    int16_t v =
        limit((int16_t)-PPM_range, channelOutputs[i], (int16_t)PPM_range) +
        2 * PPM_CH_CENTER(i);
    *data++ = v;
    total += v;
  }

  return total;
}

template <class T>
uint16_t setupPulsesPPM(T*& data, uint8_t channelsStart, int8_t channelsCount,
                        const int16_t* channels, uint8_t nChannels)
{
  uint16_t total = 0;
  int16_t PPM_range = g_model.extendedLimits ?
    // range of 0.7 .. 1.7msec
    (512*LIMIT_EXT_PERCENT/100) * 2 : 512 * 2;

  uint8_t firstCh = channelsStart;
  uint8_t lastCh = getLastPpmChannel(firstCh, channelsCount);

  for (uint32_t i = firstCh; i < lastCh; i++) {
    uint8_t index = i - firstCh;
    int16_t channelValue =
        (channels && index < nChannels) ? channels[index] : 0;
    int16_t v = limit((int16_t)-PPM_range, channelValue, (int16_t)PPM_range) +
                2 * PPM_CH_CENTER(i);
    *data++ = v;
    total += v;
  }

  return total;
}

void setupPulsesPPMTrainer()
{
  auto p_data = trainerPulsesData.ppm.pulses;
  uint16_t total = setupPulsesPPM<trainer_pulse_duration_t>(
      p_data, g_model.trainerData.channelsStart,
      g_model.trainerData.channelsCount);

  uint32_t rest = PPM_TRAINER_PERIOD_HALF_US();
  if ((uint32_t)total < rest + PPM_SAFE_MARGIN * 2)
    rest -= total;
  else
    rest = PPM_SAFE_MARGIN * 2;

  // restrict to 16 bit max
  if (rest >= USHRT_MAX - 1)
    rest = USHRT_MAX - 1;
    
  *p_data++ = (uint16_t)rest;

  // stop mark so that IRQ-based sending
  // knows when to stop
  *p_data = 0;

  trainerPulsesData.ppm.ptr = p_data;
}

static uint32_t setupPulsesPPMModule(uint8_t module, pulse_duration_t*& data,
                                     const int16_t* channels,
                                     uint8_t nChannels)
{
  auto start = data;
  setupPulsesPPM(data,
                 g_model.moduleData[module].channelsStart,
                 g_model.moduleData[module].channelsCount,
                 channels,
                 nChannels);

  // Set the final period to 1ms after which the
  // PPM will be switched OFF
  *data++ = PPM_SAFE_MARGIN * 2;

  return data - start;
}

typedef void (*ppm_telemetry_fct_t)(uint8_t module, uint8_t data, uint8_t* buffer, uint8_t* len);
static ppm_telemetry_fct_t _processTelemetryData;

static etx_serial_init ppmMLinkSerialParams = {
  .baudrate = PPM_MSB_BAUDRATE,
  .encoding = ETX_Encoding_8N1,
  .direction = ETX_Dir_RX,
  .polarity = ETX_Pol_Inverted,
};

static bool ppmInitMLinkTelemetry(uint8_t module)
{
  // Try S.PORT hardware USART (requires HW inverters)
  if (modulePortInitSerial(module, ETX_MOD_PORT_SPORT, &ppmMLinkSerialParams, true) != nullptr) {
    return true;
  }

  return false;
}

static const etx_serial_init ppmSportSerialParams = {
    .baudrate = FRSKY_SPORT_BAUDRATE,
    .encoding = ETX_Encoding_8N1,
    .direction = ETX_Dir_RX,
    .polarity = ETX_Pol_Normal,
};

static bool ppmInitSPortTelemetry(uint8_t module)
{
  // Try S.PORT hardware USART (requires HW inverters)
  if (modulePortInitSerial(module, ETX_MOD_PORT_SPORT, &ppmSportSerialParams, false) != nullptr) {
    return true;
  }

  return false;
}

static void _init_telemetry(uint8_t module, uint8_t telemetry_type)
{
  switch (telemetry_type) {
    case PPM_PROTO_TLM_MLINK:
      if (ppmInitMLinkTelemetry(module)) {
        _processTelemetryData = processExternalMLinkSerialData;
      }
      break;

    case PPM_PROTO_TLM_SPORT:
      if (ppmInitSPortTelemetry(module)) {
        _processTelemetryData = processFrskySportTelemetryData;
      }
      break;

    default:
      _processTelemetryData = nullptr;
      break;
  }
}

static void* ppmInit(uint8_t module)
{
#if defined(HARDWARE_INTERNAL_MODULE)
  // only external module supported
  if (module == INTERNAL_MODULE) return nullptr;
#endif

  auto delay = GET_MODULE_PPM_DELAY(module) * 2;
  etx_timer_config_t cfg = {
    .polarity = !GET_MODULE_PPM_POLARITY(module),
    .cmp_val = (uint16_t)delay,
  };

  auto mod_st = modulePortInitTimer(module, ETX_MOD_PORT_TIMER, &cfg);
  if (!mod_st) return nullptr;

  mixerSchedulerSetPeriod(module, PPM_PERIOD(module));

  uint8_t telemetry_type = g_model.moduleData[module].subType;
  mod_st->user_data = (void*)(uintptr_t)telemetry_type;

  _init_telemetry(module, telemetry_type);
  return (void*)mod_st;  
}

static void ppmDeInit(void* ctx)
{
  auto mod_st = (etx_module_state_t*)ctx;
  modulePortDeInit(mod_st);
  _processTelemetryData = nullptr;
}

static void ppmSendPulses(void* ctx, uint8_t* buffer, int16_t* channels, uint8_t nChannels)
{
  auto mod_st = (etx_module_state_t*)ctx;
  auto module = modulePortGetModule(mod_st);

  pulse_duration_t* pulses =
      static_cast<pulse_duration_t*>(static_cast<void*>(buffer));
  auto length = setupPulsesPPMModule(module, pulses, channels, nChannels);

  auto drv = modulePortGetTimerDrv(mod_st->tx);
  auto drv_ctx = modulePortGetCtx(mod_st->tx);
  if (!drv) return;

  auto delay = GET_MODULE_PPM_DELAY(module) * 2;
  etx_timer_config_t cfg = {
    .polarity = !GET_MODULE_PPM_POLARITY(module),
    .cmp_val = (uint16_t)delay,
  };

  drv->send(drv_ctx, &cfg, buffer, length);

  // PPM_PERIOD is not a constant! It can be set from UI
  mixerSchedulerSetPeriod(module, PPM_PERIOD(module));
}

static void ppmProcessTelemetryData(void* ctx, uint8_t data, uint8_t* buffer, uint8_t* len) {
  auto mod_st = (etx_module_state_t*)ctx;
  auto module = modulePortGetModule(mod_st);

  if (_processTelemetryData) {
    _processTelemetryData(module, data, buffer, len);
  }
}

static void ppmOnConfigChange(void* ctx)
{
  auto mod_st = (etx_module_state_t*)ctx;
  auto module = modulePortGetModule(mod_st);

  uint8_t telemetry_type = (uint8_t)(uintptr_t)mod_st->user_data;
  if (telemetry_type != g_model.moduleData[module].subType) {
    // restart during next mixer cycle
    restartModuleAsync(module, 0);
  }
}

const etx_proto_driver_t PpmDriver = {
  .protocol = PROTOCOL_CHANNELS_PPM,
  .init = ppmInit,
  .deinit = ppmDeInit,
  .sendPulses = ppmSendPulses,
  .processData = ppmProcessTelemetryData,
  .processFrame = nullptr,
  .onConfigChange = ppmOnConfigChange,
  .txCompleted = modulePortTimerTxCompleted,
};
