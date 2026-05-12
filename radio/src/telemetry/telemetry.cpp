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
#include "multi.h"
#include "os/async.h"
#include "os/sleep.h"
#include "os/task.h"
#include "os/timer.h"
#include "pulses/afhds3.h"
#include "pulses/flysky.h"
#include "mixer_scheduler.h"
#include "tasks/mixer_task.h"
#include "io/multi_protolist.h"
#include "hal/module_port.h"

#include <list>

#if !defined(SIMU)
  #include <FreeRTOS/include/FreeRTOS.h>
  #include <FreeRTOS/include/timers.h>
#endif

#include "spektrum.h"

#include <atomic>

#if defined(NATIVE_THREADS)
  #include <thread>
#endif

#if defined(CROSSFIRE)
  #include "crossfire.h"
#endif

#if defined(GHOST)
  #include "ghost.h"
#endif

#if defined(MULTIMODULE)
  #include "hitec.h"
  #include "hott.h"
  #include "multi.h"
#endif

#if  defined(MULTIMODULE) || defined(PPM)
  #include "mlink.h"
#endif

#if defined(MULTIMODULE) || defined(AFHDS2) || defined(AFHDS3)
  #include "flysky_ibus.h"
#endif

#include "telemetry/battery_monitor.h"

struct telemetry_buffer {
  uint8_t buffer[TELEMETRY_RX_PACKET_SIZE];
  uint8_t length;
};

static bool checkFlightBatteryAlerts();
static void checkFlightBatteryMissingTelemetryAfterArming();
static bool isValidManualLipoConfig(const BatteryMonitorData& config);

std::atomic<uint8_t> telemetryStreaming{0};
std::atomic<uint8_t> telemetryState{TELEMETRY_INIT};

TelemetryData telemetryData;
static rxStatStruct rxStat;

telemetry_buffer _telemetry_rx_buffer[NUM_MODULES];

static recursive_mutex_handle_t telemetryDataMutex;
static std::atomic<uint8_t> telemetryDataMutexState{0};

namespace {

enum TelemetryDataMutexState : uint8_t {
  TELEMETRY_DATA_MUTEX_UNINITIALIZED,
  TELEMETRY_DATA_MUTEX_INITIALIZING,
  TELEMETRY_DATA_MUTEX_READY,
};

void telemetryDataMutexWait()
{
#if defined(NATIVE_THREADS)
  std::this_thread::yield();
#elif defined(FREE_RTOS)
  if (scheduler_is_running()) {
    sleep_ms(1);
  }
#endif
}

}  // namespace

FlightBatteryRuntimeState flightBatteryRuntimeState[MAX_BATTERY_MONITORS];

static std::atomic<uint8_t> s_flightBatteryPublishedState[MAX_BATTERY_MONITORS];
static std::atomic<uint16_t> s_flightBatteryPublishedPromptMask[MAX_BATTERY_MONITORS];
static std::atomic<uint8_t> s_flightBatteryPublishedPromptShown[MAX_BATTERY_MONITORS];
static std::atomic<uint16_t> s_flightBatteryPromptRequestMask{0};
static std::atomic<uint8_t> s_flightBatteryPublishedArmingAllowed{1};
static std::atomic<uint8_t> s_flightBatteryPublishedBlockReason{
    uint8_t(ArmingBlockReason::None)};
static std::atomic<uint8_t> s_armingBlockReason{uint8_t(ArmingBlockReason::None)};

static bool flightBatteryMonitorArmingAllowed(const BatteryMonitorData& config,
                                              FlightBatterySessionState state)
{
  if (!config.enabled) return true;
  if (config.compatiblePackMask == 0 && !isValidManualLipoConfig(config)) {
    return false;
  }
  return state != FlightBatterySessionState::NeedsConfirmation &&
         state != FlightBatterySessionState::VoltageMismatch &&
         state != FlightBatterySessionState::NeedsConfiguration;
}

static ArmingBlockReason flightBatteryMonitorBlockReason(
    const BatteryMonitorData& config, FlightBatterySessionState state)
{
  if (config.compatiblePackMask == 0 && !isValidManualLipoConfig(config)) {
    return ArmingBlockReason::BatteryNeedsConfiguration;
  }

  switch (state) {
    case FlightBatterySessionState::VoltageMismatch:
      return ArmingBlockReason::BatteryVoltageMismatch;
    case FlightBatterySessionState::NeedsConfiguration:
      return ArmingBlockReason::BatteryNeedsConfiguration;
    case FlightBatterySessionState::NeedsConfirmation:
    default:
      return ArmingBlockReason::BatteryUnknown;
  }
}

static void publishFlightBatteryArmingSnapshot()
{
  ArmingBlockReason reason = ArmingBlockReason::None;
  bool allowed = true;
  for (uint8_t i = 0; i < MAX_BATTERY_MONITORS; i++) {
    const auto& config = g_model.batteryMonitors[i];
    if (!config.enabled) continue;

    const auto state = FlightBatterySessionState(
        s_flightBatteryPublishedState[i].load(std::memory_order_acquire));
    if (!flightBatteryMonitorArmingAllowed(config, state)) {
      allowed = false;
      reason = flightBatteryMonitorBlockReason(config, state);
      break;
    }
  }

  s_flightBatteryPublishedBlockReason.store(uint8_t(reason),
                                            std::memory_order_release);
  s_flightBatteryPublishedArmingAllowed.store(allowed ? 1 : 0,
                                             std::memory_order_release);
}

static void publishFlightBatteryRuntimeState(uint8_t monitor)
{
  if (monitor >= MAX_BATTERY_MONITORS) return;

  const auto& runtime = flightBatteryRuntimeState[monitor];
  s_flightBatteryPublishedPromptMask[monitor].store(runtime.promptPackMask,
                                                    std::memory_order_release);
  s_flightBatteryPublishedPromptShown[monitor].store(runtime.promptShown ? 1 : 0,
                                                     std::memory_order_release);
  s_flightBatteryPublishedState[monitor].store(uint8_t(runtime.state),
                                               std::memory_order_release);
  publishFlightBatteryArmingSnapshot();
}

static FlightBatterySessionState publishedFlightBatteryState(uint8_t monitor)
{
  if (monitor >= MAX_BATTERY_MONITORS) return FlightBatterySessionState::Unknown;
  return FlightBatterySessionState(
      s_flightBatteryPublishedState[monitor].load(std::memory_order_acquire));
}

void setArmingBlockReason(ArmingBlockReason reason) {
  uint8_t expected = uint8_t(ArmingBlockReason::None);
  s_armingBlockReason.compare_exchange_strong(expected, uint8_t(reason),
                                              std::memory_order_acq_rel);
}

ArmingBlockReason consumeArmingBlockReason() {
  return ArmingBlockReason(s_armingBlockReason.exchange(
      uint8_t(ArmingBlockReason::None), std::memory_order_acq_rel));
}

void telemetryInit()
{
  uint8_t state =
      telemetryDataMutexState.load(std::memory_order_acquire);
  if (state == TELEMETRY_DATA_MUTEX_READY) {
    return;
  }

  uint8_t expected = TELEMETRY_DATA_MUTEX_UNINITIALIZED;
  if (telemetryDataMutexState.compare_exchange_strong(
          expected, TELEMETRY_DATA_MUTEX_INITIALIZING,
          std::memory_order_acq_rel, std::memory_order_acquire)) {
    recursive_mutex_create(&telemetryDataMutex);
    telemetryDataMutexState.store(TELEMETRY_DATA_MUTEX_READY,
                                  std::memory_order_release);
    return;
  }

  while (telemetryDataMutexState.load(std::memory_order_acquire) !=
         TELEMETRY_DATA_MUTEX_READY) {
    telemetryDataMutexWait();
  }
}

void telemetryDataLock()
{
  telemetryInit();
  recursive_mutex_lock(&telemetryDataMutex);
}

bool telemetryDataTryLock()
{
  telemetryInit();
  return recursive_mutex_trylock(&telemetryDataMutex);
}

void telemetryDataUnlock()
{
  recursive_mutex_unlock(&telemetryDataMutex);
}

static void clearTelemetryRxBuffers()
{
  memset(_telemetry_rx_buffer, 0, sizeof(_telemetry_rx_buffer));
}

uint8_t* getTelemetryRxBuffer(uint8_t moduleIdx)
{
  return _telemetry_rx_buffer[moduleIdx].buffer;
}

uint8_t &getTelemetryRxBufferCount(uint8_t moduleIdx)
{
  return _telemetry_rx_buffer[moduleIdx].length;
}

rxStatStruct *getRxStatLabels() {
  // default to RSSI/db notation
  rxStat.label = STR_RXSTAT_LABEL_RSSI;
  rxStat.unit  = STR_RXSTAT_UNIT_DBM;

  // Currently we can only display a single rx stat in settings/telemetry.
  // If both modules are used we choose the internal one
  // TODO: have to rx stat sections in settings/telemetry
  uint8_t moduleToUse = INTERNAL_MODULE;

  if(g_model.moduleData[INTERNAL_MODULE].type == MODULE_TYPE_NONE && 
     g_model.moduleData[EXTERNAL_MODULE].type != MODULE_TYPE_NONE) {
    moduleToUse = EXTERNAL_MODULE;
  }

  uint8_t moduleType = g_model.moduleData[moduleToUse].type;

  switch (moduleType) {
#if defined(MULTIMODULE)
    case MODULE_TYPE_MULTIMODULE: {
      uint8_t multiProtocol = g_model.moduleData[moduleToUse].multi.rfProtocol;

      if (multiProtocol == MODULE_SUBTYPE_MULTI_FS_AFHDS2A ||
          multiProtocol == MODULE_SUBTYPE_MULTI_HOTT ||
          multiProtocol == MODULE_SUBTYPE_MULTI_MLINK) {
        rxStat.label = STR_RXSTAT_LABEL_RQLY;
        rxStat.unit = STR_RXSTAT_UNIT_PERCENT;
      }
    } break;
#endif
    case MODULE_TYPE_PPM:
      if (g_model.moduleData[moduleToUse].subType == PPM_PROTO_TLM_MLINK) {
        rxStat.label = STR_RXSTAT_LABEL_RQLY;
        rxStat.unit = STR_RXSTAT_UNIT_PERCENT;
      }
      break;

    case MODULE_TYPE_CROSSFIRE:
    case MODULE_TYPE_GHOST:
      rxStat.label = STR_RXSTAT_LABEL_RQLY;
      rxStat.unit = STR_RXSTAT_UNIT_PERCENT;
      break;

#if defined(RADIO_NV14_FAMILY) && defined(AFHDS2)
    case MODULE_TYPE_FLYSKY_AFHDS2A:
      extern uint32_t NV14internalModuleFwVersion;

      if (moduleToUse == INTERNAL_MODULE) {
        if (NV14internalModuleFwVersion >= 0x1000E) {
          rxStat.label = STR_RXSTAT_LABEL_SIGNAL;
          rxStat.unit = STR_RXSTAT_UNIT_NOUNIT;
        }
      }
      break;
#endif
  }

  return &rxStat;
}

static std::atomic_uint telemetryPollingDepth{0};

class TelemetryPollingGuard
{
  public:
    TelemetryPollingGuard()
    {
      telemetryPollingDepth.fetch_add(1, std::memory_order_acq_rel);
    }

    ~TelemetryPollingGuard()
    {
      telemetryPollingDepth.fetch_sub(1, std::memory_order_acq_rel);
    }

    TelemetryPollingGuard(const TelemetryPollingGuard&) = delete;
    TelemetryPollingGuard& operator=(const TelemetryPollingGuard&) = delete;
};

bool telemetryIsPolling()
{
  return telemetryPollingDepth.load(std::memory_order_acquire) > 0;
}

static void (*telemetryMirrorSendByte)(void*, uint8_t) = nullptr;
static void* telemetryMirrorSendByteCtx = nullptr;

void telemetrySetMirrorCb(void* ctx, void (*fct)(void*, uint8_t))
{
  telemetryMirrorSendByte = nullptr;
  telemetryMirrorSendByteCtx = ctx;
  telemetryMirrorSendByte = fct;
}

void telemetryMirrorSend(uint8_t data)
{
  auto _sendByte = telemetryMirrorSendByte;
  auto _ctx = telemetryMirrorSendByteCtx;

  if (_sendByte) {
    _sendByte(_ctx, data);
  }
}

static timer_handle_t telemetryTimer = TIMER_INITIALIZER;

static void telemetryTimerCb(timer_handle_t* h)
{
  DEBUG_TIMER_START(debugTimerTelemetryWakeup);
  telemetryWakeup();
  DEBUG_TIMER_STOP(debugTimerTelemetryWakeup);
}

void telemetryStart()
{
  if (!timer_is_created(&telemetryTimer)) {
    timer_create(&telemetryTimer, telemetryTimerCb, "Telem", 2, true);
  }

  clearTelemetryRxBuffers();
  timer_start(&telemetryTimer);
}

void telemetryStop()
{
  if (!timer_is_created(&telemetryTimer)) {
    timer_stop(&telemetryTimer);
  }
}

static AsyncExclusiveFlag _poll_frame_queued[NUM_MODULES];

static void _poll_frame(void *pvParameter1, uint32_t ulParameter2)
{
  TelemetryPollingGuard polling;
  MixerTaskLockGuard mixerLock;

  auto drv = (const etx_proto_driver_t*)pvParameter1;
  auto module = (uint8_t)ulParameter2;
  _poll_frame_queued[module].clear();

  auto mod = pulsesGetModuleDriver(module);
  if (!mod || !mod->drv || !mod->ctx || (drv != mod->drv))
    return;

  auto ctx = mod->ctx;
  auto mod_st = (etx_module_state_t*)ctx;
  auto serial_drv = modulePortGetSerialDrv(mod_st->rx);
  auto serial_ctx = modulePortGetCtx(mod_st->rx);

  if (!serial_drv || !serial_ctx || !serial_drv->copyRxBuffer)
    return;

  uint8_t frame[TELEMETRY_RX_PACKET_SIZE];

  int frame_len = serial_drv->copyRxBuffer(serial_ctx, frame, TELEMETRY_RX_PACKET_SIZE);
  if (frame_len > 0) {

    LOG_TELEMETRY_WRITE_START();
    for (int i = 0; i < frame_len; i++) {
      telemetryMirrorSend(frame[i]);
      LOG_TELEMETRY_WRITE_BYTE(frame[i]);
    }

    uint8_t* rxBuffer = getTelemetryRxBuffer(module);
    uint8_t& rxBufferCount = getTelemetryRxBufferCount(module);
    drv->processFrame(ctx, frame, frame_len, rxBuffer, &rxBufferCount);
  }

}

void telemetryFrameTrigger_ISR(uint8_t module, const etx_proto_driver_t* drv)
{
  async_call_isr(_poll_frame, &_poll_frame_queued[module], (void*)drv, module);
}

inline bool isBadAntennaDetected()
{
  if (!isRasValueValid())
    return false;

  if (telemetryData.swrInternal.isFresh() &&
      telemetryData.swrInternal.value() > FRSKY_BAD_ANTENNA_THRESHOLD)
    return true;

  if (telemetryData.swrExternal.isFresh() &&
      telemetryData.swrExternal.value() > FRSKY_BAD_ANTENNA_THRESHOLD)
    return true;

  return false;
}

static inline void pollTelemetry(uint8_t module, const etx_proto_driver_t* drv, void* ctx)
{
  if (!drv || !drv->processData) return;

  auto mod_st = (etx_module_state_t*)ctx;
  auto serial_drv = modulePortGetSerialDrv(mod_st->rx);
  auto serial_ctx = modulePortGetCtx(mod_st->rx);

  if (!serial_drv  || !serial_ctx || !serial_drv->getByte)
    return;

  uint8_t* rxBuffer = getTelemetryRxBuffer(module);
  uint8_t& rxBufferCount = getTelemetryRxBufferCount(module);

  uint8_t data;
  if (serial_drv->getByte(serial_ctx, &data) > 0) {
    LOG_TELEMETRY_WRITE_START();
    do {
      telemetryMirrorSend(data);
      drv->processData(ctx, data, rxBuffer, &rxBufferCount);
      LOG_TELEMETRY_WRITE_BYTE(data);
    } while (serial_drv->getByte(serial_ctx, &data) > 0);
  }
}

void telemetryWakeup()
{
  TelemetryDataTryLock telemetryLock;
  if (!telemetryLock) return;

  {
    TelemetryPollingGuard polling;
    MixerTaskLockGuard mixerLock;
    for (uint8_t i = 0; i < MAX_MODULES; i++) {
      auto mod = pulsesGetModuleDriver(i);
      if (!mod) continue;
      pollTelemetry(i, mod->drv, mod->ctx);
    }
  }

  for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
    const TelemetrySensor& sensor = g_model.telemetrySensors[i];
    if (sensor.type == TELEM_TYPE_CALCULATED) {
      telemetryItems[i].eval(sensor);
    }
  }

#if defined(VARIO)
  if (TELEMETRY_STREAMING() && !IS_FAI_ENABLED()) {
    varioWakeup();
  }
#endif

  static tmr10ms_t flightBatteryCheckTime = 0;
  if (int32_t(get_tmr10ms() - flightBatteryCheckTime) > 0) {
    flightBatteryCheckTime = get_tmr10ms() + 100;
    updateFlightBatterySessions();
    checkFlightBatteryMissingTelemetryAfterArming();
    if (!g_model.disableTelemetryWarning) {
      checkFlightBatteryAlerts();
    }
  }

  static tmr10ms_t alarmsCheckTime = 0;
#define SCHEDULE_NEXT_ALARMS_CHECK(seconds) \
  alarmsCheckTime = get_tmr10ms() + (100 * (seconds))
  if (int32_t(get_tmr10ms() - alarmsCheckTime) > 0) {
    SCHEDULE_NEXT_ALARMS_CHECK(1 /*second*/);

    bool sensorLost = false;
    for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
      if (isTelemetryFieldAvailable(i)) {
        TelemetryItem& item = telemetryItems[i];
        if (item.timeout == 0) {
          TelemetrySensor* sensor = &g_model.telemetrySensors[i];
          if (sensor->unit != UNIT_DATETIME) {
            item.setOld();
            sensorLost = true;
          }
        }
      }
    }

    if (sensorLost && TELEMETRY_STREAMING() &&
        !g_model.disableTelemetryWarning) {
      audioEvent(AU_SENSOR_LOST);
    }

#if defined(PXX1) || defined(PXX2)
    if (isBadAntennaDetected()) {
      AUDIO_RAS_RED();
      if (POPUP_WARNING_ON_UI_TASK(STR_WARNING, STR_ANTENNAPROBLEM))
        SCHEDULE_NEXT_ALARMS_CHECK(10 /*seconds*/);
    }
#endif

    if (!g_model.disableTelemetryWarning) {
      if (TELEMETRY_STREAMING()) {
        if (TELEMETRY_RSSI() < g_model.rfAlarms.critical) {
          AUDIO_RSSI_RED();
          SCHEDULE_NEXT_ALARMS_CHECK(10 /*seconds*/);
        } else if (TELEMETRY_RSSI() < g_model.rfAlarms.warning) {
          AUDIO_RSSI_ORANGE();
          SCHEDULE_NEXT_ALARMS_CHECK(10 /*seconds*/);
        }
      }

      if (TELEMETRY_STREAMING()) {
        if (telemetryState == TELEMETRY_INIT) {
          AUDIO_TELEMETRY_CONNECTED();
        } else if (telemetryState == TELEMETRY_KO) {
          AUDIO_TELEMETRY_BACK();

#if defined(CROSSFIRE)
          // TODO: move to crossfire code
#if defined(HARDWARE_EXTERNAL_MODULE)
          if (isModuleCrossfire(EXTERNAL_MODULE)) {
            moduleState[EXTERNAL_MODULE].counter = CRSF_FRAME_MODELID;
          }
#endif

#if defined(HARDWARE_INTERNAL_MODULE)
          if (isModuleCrossfire(INTERNAL_MODULE)) {
            moduleState[INTERNAL_MODULE].counter = CRSF_FRAME_MODELID;
          }
#endif
#endif
        }
        telemetryState = TELEMETRY_OK;
      } else if (telemetryState == TELEMETRY_OK) {
        telemetryState = TELEMETRY_KO;
        if (!isModuleInBeepMode()) {
          AUDIO_TELEMETRY_LOST();
        }
      }
    }
  }
}

void telemetryInterrupt10ms()
{
  TelemetryDataTryLock telemetryLock;
  if (!telemetryLock) return;

  uint8_t streaming = telemetryStreaming.load(std::memory_order_acquire);
  if (streaming > 0) {
    bool tick160ms = (streaming & 0x0F) == 0;
    for (int i=0; i<MAX_TELEMETRY_SENSORS; i++) {
      const TelemetrySensor & sensor = g_model.telemetrySensors[i];
      if (sensor.type == TELEM_TYPE_CALCULATED) {
        telemetryItems[i].per10ms(sensor);
      }
      if (tick160ms && telemetryItems[i].timeout > 0) {
        telemetryItems[i].timeout--;
      }
    }
    while (streaming > 0 &&
           !telemetryStreaming.compare_exchange_weak(
               streaming, streaming - 1, std::memory_order_acq_rel,
               std::memory_order_acquire)) {
    }
  }
  else {
#if !defined(SIMU)
    telemetryData.rssi.reset();
#endif
    for (auto & telemetryItem: telemetryItems) {
      if (telemetryItem.isAvailable()) {
        telemetryItem.setOld();
      }
    }
  }
}

void telemetryReset()
{
  TelemetryDataLock telemetryLock;

  telemetryData.clear();

  for (auto & telemetryItem : telemetryItems) {
    telemetryItem.clear();
  }

  telemetryStreaming = 0; // reset counter only if valid telemetry packets are being detected
  telemetryState = TELEMETRY_INIT;

  resetFlightBatteryRuntimeState();
}

#if defined(LOG_TELEMETRY) && !defined(SIMU)
extern FIL g_telemetryFile;
void logTelemetryWriteStart()
{
  static tmr10ms_t lastTime = 0;
  tmr10ms_t newTime = get_tmr10ms();
  if (lastTime != newTime) {
    struct gtm utm;
    gettime(&utm);
    f_printf(&g_telemetryFile, "\r\n%4d-%02d-%02d,%02d:%02d:%02d.%02d0:",
             utm.tm_year + TM_YEAR_BASE, utm.tm_mon + 1, utm.tm_mday,
             utm.tm_hour, utm.tm_min, utm.tm_sec, rtcGetMs100());
    lastTime = newTime;
  }
}

void logTelemetryWriteByte(uint8_t data)
{
  f_printf(&g_telemetryFile, " %02X", data);
}
#endif

OutputTelemetryBuffer outputTelemetryBuffer __DMA_NO_CACHE;

#if defined(LUA)
TelemetryQueue* luaInputTelemetryFifo = nullptr;
#if defined(COLORLCD)
std::list<TelemetryQueue*> telemetryQueues;

void registerTelemetryQueue(TelemetryQueue* queue)
{
  telemetryQueues.emplace_back(queue);
}

void deregisterTelemetryQueue(TelemetryQueue* queue)
{
  telemetryQueues.remove(queue);
}
#endif

static void pushDataToQueue(TelemetryQueue* queue, uint8_t* data, int length)
{
  if (queue && queue->hasSpace(length)) {
    for (int i = 0; i < length; i += 1) {
      queue->push(data[i]);
    }
  }
}

void pushTelemetryDataToQueues(uint8_t* data, int length)
{
#if defined(COLORLCD)
  for (auto it = telemetryQueues.cbegin(); it != telemetryQueues.cend(); ++it)
    pushDataToQueue(*it, data, length);
#endif
  pushDataToQueue(luaInputTelemetryFifo, data, length);
}
#endif

#if defined(HARDWARE_INTERNAL_MODULE)
static ModuleSyncStatus moduleSyncStatus[NUM_MODULES];

ModuleSyncStatus &getModuleSyncStatus(uint8_t moduleIdx)
{
  return moduleSyncStatus[moduleIdx];
}
#else
static ModuleSyncStatus moduleSyncStatus;

ModuleSyncStatus &getModuleSyncStatus(uint8_t moduleIdx)
{
  return moduleSyncStatus;
}
#endif

ModuleSyncStatus::ModuleSyncStatus()
{
  memset(this, 0, sizeof(ModuleSyncStatus));
}

void ModuleSyncStatus::update(uint16_t newRefreshRate, int16_t newInputLag)
{
  if (!newRefreshRate)
    return;
  
  if (newRefreshRate < MIN_REFRESH_RATE)
    newRefreshRate = newRefreshRate * (MIN_REFRESH_RATE / (newRefreshRate + 1));
  else if (newRefreshRate > MAX_REFRESH_RATE)
    newRefreshRate = MAX_REFRESH_RATE;

  refreshRate = newRefreshRate;
  inputLag    = newInputLag;
  currentLag  = newInputLag;
  lastUpdate  = get_tmr10ms();

#if 0
  TRACE("[SYNC] update rate = %dus; lag = %dus",refreshRate,currentLag);
#endif
}

void ModuleSyncStatus::invalidate() {
  //make invalid after use
  currentLag = 0;
}

uint16_t ModuleSyncStatus::getAdjustedRefreshRate()
{
  int16_t lag = currentLag;
  int32_t newRefreshRate = refreshRate;

  if (lag == 0) {
    return refreshRate;
  }
  
  newRefreshRate += lag;
  
  if (newRefreshRate < MIN_REFRESH_RATE) {
      newRefreshRate = MIN_REFRESH_RATE;
  }
  else if (newRefreshRate > MAX_REFRESH_RATE) {
    newRefreshRate = MAX_REFRESH_RATE;
  }

  currentLag -= newRefreshRate - refreshRate;
#if 0
  TRACE("[SYNC] mod rate = %dus; lag = %dus",newRefreshRate,currentLag);
#endif
  
  return (uint16_t)newRefreshRate;
}

void ModuleSyncStatus::getRefreshString(char * statusText)
{
  if (!isValid()) {
    return;
  }

  char * tmp = statusText;
#if defined(DEBUG)
  *tmp++ = 'L';
  tmp = strAppendSigned(tmp, inputLag, 5);
  tmp = strAppend(tmp, "R");
  tmp = strAppendUnsigned(tmp, refreshRate, 5);
#else
  tmp = strAppend(tmp, "Sync ");
  tmp = strAppendUnsigned(tmp, refreshRate);
#endif
  tmp = strAppend(tmp, "us");
}

static bool isFlightBatteryVoltageUnit(uint8_t unit)
{
  return unit == UNIT_VOLTS || unit == UNIT_CELLS;
}

static bool isValidManualLipoConfig(const BatteryMonitorData& config)
{
  return config.batteryType == BATTERY_TYPE_LIPO && config.cellCount > 0 &&
         config.capacity > 0;
}

static int sourceToTelemetryIndex(int8_t source)
{
  if (source <= 0) return -1;

  const int index = source - 1;
  return index < MAX_TELEMETRY_SENSORS ? index : -1;
}

static int findFlightBatteryVoltageSensor(const BatteryMonitorData& config)
{
  const int index = sourceToTelemetryIndex(config.sourceIndex);
  if (index < 0) return -1;

  return isFlightBatteryVoltageUnit(g_model.telemetrySensors[index].unit) ? index
                                                                          : -1;
}

static int findFlightBatteryCapacitySensor(const BatteryMonitorData& config)
{
  const int index = sourceToTelemetryIndex(config.currentIndex);
  if (index < 0) return -1;

  return g_model.telemetrySensors[index].unit == UNIT_MAH ? index : -1;
}

static bool isUsableTelemetrySensor(int sensorIndex)
{
  if (sensorIndex < 0 || sensorIndex >= MAX_TELEMETRY_SENSORS) return false;
  const TelemetrySensor& sensor = g_model.telemetrySensors[sensorIndex];
  TelemetryItem& item = telemetryItems[sensorIndex];
  return sensor.isAvailable() && item.isAvailable() && !item.isOld();
}

static int32_t telemetrySensorValue(int sensorIndex, uint8_t destUnit,
                                    uint8_t destPrec)
{
  if (sensorIndex < 0 || sensorIndex >= MAX_TELEMETRY_SENSORS) return 0;
  const TelemetrySensor& sensor = g_model.telemetrySensors[sensorIndex];
  const TelemetryItem& item = telemetryItems[sensorIndex];
  const uint8_t sourceUnit =
      sensor.unit == UNIT_CELLS ? uint8_t(UNIT_VOLTS) : sensor.unit;
  return convertTelemetryValue(item.value, sourceUnit, sensor.prec, destUnit,
                               destPrec);
}

static bool readFlightBatteryVoltage(const BatteryMonitorData& config,
                                     uint16_t& packVoltageCv)
{
  const int sensorIndex = findFlightBatteryVoltageSensor(config);
  if (sensorIndex < 0 || !isUsableTelemetrySensor(sensorIndex)) return false;

  const int32_t value = telemetrySensorValue(sensorIndex, UNIT_VOLTS, 2);
  if (value < 0) return false;

  packVoltageCv = uint16_t(value > UINT16_MAX ? UINT16_MAX : value);
  return true;
}

static bool isFlightBatteryVoltagePresent(const BatteryMonitorData& config)
{
  uint16_t packVoltageCv = 0;
  return readFlightBatteryVoltage(config, packVoltageCv) &&
         packVoltageCv >= FLIGHT_BATTERY_NO_BATTERY_MAX_CV;
}

static bool isFlightBatteryCapacityPresent(const BatteryMonitorData& config)
{
  const int sensorIndex = findFlightBatteryCapacitySensor(config);
  return sensorIndex >= 0 && isUsableTelemetrySensor(sensorIndex);
}

static bool hasMissingArmedFlightBatteryTelemetry(const BatteryMonitorData& config)
{
  if (!config.enabled) return false;
  if (!isFlightBatteryVoltagePresent(config)) return true;
  return config.capAlertEnabled && !isFlightBatteryCapacityPresent(config);
}

static uint8_t buildVoltageCompatiblePackMask(const BatteryMonitorData& config,
                                              uint16_t packVoltageCv,
                                              uint16_t& matchingMask)
{
  uint8_t matchCount = 0;
  matchingMask = 0;

  for (uint8_t slot = 0; slot < MAX_BATTERY_PACKS; slot++) {
    const uint16_t slotBit = uint16_t(1u << slot);
    if ((config.compatiblePackMask & slotBit) == 0) continue;

    const BatteryPackData& pack = g_eeGeneral.batteryPacks[slot];
    if (!pack.active) continue;

    if (flightBatteryPackMatchesLipo(packVoltageCv, pack.cellCount)) {
      matchingMask |= slotBit;
      matchCount++;
    }
  }

  return matchCount;
}

static uint8_t firstPackSlotFromMask(uint16_t mask)
{
  for (uint8_t slot = 0; slot < MAX_BATTERY_PACKS; slot++) {
    if (mask & (1u << slot)) return slot + 1;
  }
  return 0;
}

static bool confirmedPackMatchesVoltage(const BatteryMonitorData& config,
                                        const FlightBatteryRuntimeState& runtime,
                                        uint16_t packVoltageCv)
{
  if (runtime.confirmedPackSlot > 0) {
    const uint8_t slot = runtime.confirmedPackSlot - 1;
    if (slot >= MAX_BATTERY_PACKS) return false;
    const BatteryPackData& pack = g_eeGeneral.batteryPacks[slot];
    return pack.active &&
           flightBatteryPackMatchesLipo(packVoltageCv, pack.cellCount);
  }

  return isValidManualLipoConfig(config) &&
         flightBatteryPackMatchesLipo(packVoltageCv, config.cellCount);
}

void resetFlightBatteryRuntimeState()
{
  for (uint8_t i = 0; i < MAX_BATTERY_MONITORS; i++) {
    flightBatteryRuntimeState[i] = FlightBatteryRuntimeState();
    publishFlightBatteryRuntimeState(i);
  }
  s_flightBatteryPromptRequestMask.store(0, std::memory_order_release);
}

void invalidateFlightBatteryMonitor(uint8_t monitor)
{
  if (monitor >= MAX_BATTERY_MONITORS) return;

  TelemetryDataLock telemetryLock;
  flightBatteryRuntimeState[monitor] = FlightBatteryRuntimeState();
  publishFlightBatteryRuntimeState(monitor);
}

void invalidateFlightBatteryPackSlot(uint8_t slot)
{
  if (slot >= MAX_BATTERY_PACKS) return;

  const uint8_t selectedPackSlot = slot + 1;
  const uint16_t slotBit = uint16_t(1u << slot);
  TelemetryDataLock telemetryLock;
  for (uint8_t i = 0; i < MAX_BATTERY_MONITORS; i++) {
    const auto& config = g_model.batteryMonitors[i];
    if (config.selectedPackSlot == selectedPackSlot ||
        (config.compatiblePackMask & slotBit) != 0) {
      flightBatteryRuntimeState[i] = FlightBatteryRuntimeState();
      publishFlightBatteryRuntimeState(i);
    }
  }
}

static void confirmFlightBatteryPackImpl(uint8_t monitor, uint8_t selectedPackSlot);

void updateFlightBatterySessions()
{
  for (uint8_t monitorIndex = 0; monitorIndex < MAX_BATTERY_MONITORS; monitorIndex++) {
    BatteryMonitorData& config = g_model.batteryMonitors[monitorIndex];
    if (!config.enabled) {
      flightBatteryRuntimeState[monitorIndex] = FlightBatteryRuntimeState();
      publishFlightBatteryRuntimeState(monitorIndex);
      continue;
    }

    FlightBatteryRuntimeState& runtime = flightBatteryRuntimeState[monitorIndex];
    FlightBatterySessionState currentState = runtime.state;

    uint16_t packVoltageCv = 0;
    const bool voltageFresh = readFlightBatteryVoltage(config, packVoltageCv);
    const bool isArmed = isModelArmedState();

    if (currentState == FlightBatterySessionState::Unknown ||
        currentState == FlightBatterySessionState::WaitingForVoltage) {
      if (!voltageFresh) {
        runtime.state = FlightBatterySessionState::WaitingForVoltage;
        runtime.presentSeconds = 0;
      } else if (packVoltageCv < FLIGHT_BATTERY_NO_BATTERY_MAX_CV) {
        runtime.presentSeconds = 0;
        runtime.absentSeconds++;
        if (runtime.absentSeconds >= FLIGHT_BATTERY_NO_BATTERY_DEBOUNCE_SECONDS) {
          runtime = FlightBatteryRuntimeState();
          runtime.state = FlightBatterySessionState::NoBatteryObserved;
        }
      } else {
        runtime.absentSeconds = 0;
        runtime.presentSeconds++;
        if (runtime.presentSeconds >= FLIGHT_BATTERY_PRESENT_DEBOUNCE_SECONDS) {
          uint16_t matchingMask = 0;
          const uint8_t matchCount = buildVoltageCompatiblePackMask(
              config, packVoltageCv, matchingMask);

          if (matchCount == 1) {
            confirmFlightBatteryPackImpl(monitorIndex,
                                         firstPackSlotFromMask(matchingMask));
          } else if (matchCount > 1) {
            runtime.state = FlightBatterySessionState::NeedsConfirmation;
            runtime.promptPackMask = matchingMask;
            runtime.promptShown = false;
          } else {
            if (config.compatiblePackMask == 0 && isValidManualLipoConfig(config)) {
              if (flightBatteryPackMatchesLipo(packVoltageCv, config.cellCount)) {
                runtime.state = FlightBatterySessionState::NeedsConfirmation;
                runtime.promptPackMask = 0;
                runtime.promptShown = false;
              } else {
                runtime.state = FlightBatterySessionState::VoltageMismatch;
              }
            } else if (config.compatiblePackMask == 0) {
              runtime.state = FlightBatterySessionState::NeedsConfiguration;
            } else {
              runtime.state = FlightBatterySessionState::VoltageMismatch;
            }
          }
        }
      }
    } else if (currentState == FlightBatterySessionState::Confirmed ||
               currentState == FlightBatterySessionState::ConfirmedWaitingForVoltage) {
      if (!voltageFresh) {
        if (!isArmed) {
          runtime.state = FlightBatterySessionState::ConfirmedWaitingForVoltage;
        }
      } else if (packVoltageCv < FLIGHT_BATTERY_NO_BATTERY_MAX_CV) {
        runtime.presentSeconds = 0;
        runtime.absentSeconds++;
        if (runtime.absentSeconds >= FLIGHT_BATTERY_NO_BATTERY_DEBOUNCE_SECONDS && !isArmed) {
          runtime = FlightBatteryRuntimeState();
          runtime.state = FlightBatterySessionState::NoBatteryObserved;
        }
      } else {
        runtime.presentSeconds = 0;
        runtime.absentSeconds = 0;
        if (!isArmed && !confirmedPackMatchesVoltage(config, runtime, packVoltageCv)) {
          runtime.state = FlightBatterySessionState::VoltageMismatch;
        } else {
          runtime.state = FlightBatterySessionState::Confirmed;
        }
      }
    } else if (currentState == FlightBatterySessionState::NoBatteryObserved) {
      if (voltageFresh && packVoltageCv >= FLIGHT_BATTERY_NO_BATTERY_MAX_CV) {
        runtime.presentSeconds++;
        if (runtime.presentSeconds >= FLIGHT_BATTERY_PRESENT_DEBOUNCE_SECONDS) {
          runtime.presentSeconds = 0;
          runtime.absentSeconds = 0;
          runtime.state = FlightBatterySessionState::Unknown;
        }
      } else {
        runtime.presentSeconds = 0;
      }
    } else if (currentState == FlightBatterySessionState::NeedsConfirmation ||
               currentState == FlightBatterySessionState::VoltageMismatch ||
               currentState == FlightBatterySessionState::NeedsConfiguration) {
      if (!voltageFresh) {
        runtime.state = FlightBatterySessionState::WaitingForVoltage;
        runtime.promptPackMask = 0;
      } else if (packVoltageCv < FLIGHT_BATTERY_NO_BATTERY_MAX_CV) {
        runtime.presentSeconds = 0;
        runtime.absentSeconds++;
        if (runtime.absentSeconds >= FLIGHT_BATTERY_NO_BATTERY_DEBOUNCE_SECONDS) {
          runtime = FlightBatteryRuntimeState();
          runtime.state = FlightBatterySessionState::NoBatteryObserved;
        }
      } else {
        runtime.presentSeconds = 0;
        runtime.absentSeconds = 0;

        if (currentState == FlightBatterySessionState::NeedsConfirmation) {
          uint16_t matchingMask = 0;
          const uint8_t matchCount = buildVoltageCompatiblePackMask(
              config, packVoltageCv, matchingMask);

          if (matchCount == 1) {
            confirmFlightBatteryPackImpl(monitorIndex,
                                         firstPackSlotFromMask(matchingMask));
          } else if (matchCount > 1) {
            runtime.promptPackMask = matchingMask;
          } else if (!flightBatteryPromptAllowsManual(monitorIndex) ||
                     !flightBatteryPackMatchesLipo(packVoltageCv,
                                                   config.cellCount)) {
            runtime.promptPackMask = 0;
            runtime.state = FlightBatterySessionState::VoltageMismatch;
          }
        }
      }
    }

    publishFlightBatteryRuntimeState(monitorIndex);
  }
}

static void confirmFlightBatteryPackImpl(uint8_t monitor, uint8_t selectedPackSlot)
{
  BatteryMonitorData& config = g_model.batteryMonitors[monitor];
  FlightBatteryRuntimeState& runtime = flightBatteryRuntimeState[monitor];

  if (selectedPackSlot > 0) {
    uint8_t slot = selectedPackSlot - 1;
    if (slot >= MAX_BATTERY_PACKS || !g_eeGeneral.batteryPacks[slot].active)
      return;

    auto& pack = g_eeGeneral.batteryPacks[slot];
    config.batteryType = BATTERY_TYPE_LIPO;
    config.cellCount = pack.cellCount;
    config.capacity = pack.capacity;
    config.selectedPackSlot = selectedPackSlot;
    runtime.confirmedPackSlot = selectedPackSlot;
    storageDirty(EE_MODEL);
  } else {
    if (!isValidManualLipoConfig(config)) return;
    config.selectedPackSlot = 0;
    runtime.confirmedPackSlot = 0;
  }

  int8_t capSensorIdx = findFlightBatteryCapacitySensor(config);
  int32_t baseline = 0;
  if (capSensorIdx >= 0 && isUsableTelemetrySensor(capSensorIdx)) {
    baseline = telemetrySensorValue(capSensorIdx, UNIT_MAH, 0);
  }
  runtime.consumedBaselineMah = baseline;

  runtime.state = FlightBatterySessionState::Confirmed;
  runtime.capacityMask = 0;
  runtime.voltageLowSeconds = 0;
  runtime.voltageAlerted = false;
  runtime.promptShown = true;
  runtime.promptPackMask = 0;
}

FlightBatterySessionState flightBatterySessionState(uint8_t monitor)
{
  return publishedFlightBatteryState(monitor);
}

bool flightBatteryNeedsPrompt(uint8_t* monitor)
{
  const uint16_t requestedMask =
      s_flightBatteryPromptRequestMask.load(std::memory_order_acquire);
  for (uint8_t i = 0; i < MAX_BATTERY_MONITORS; i++) {
    if (g_model.batteryMonitors[i].enabled) {
      FlightBatterySessionState state = publishedFlightBatteryState(i);
      if (state == FlightBatterySessionState::NeedsConfirmation &&
          (!s_flightBatteryPublishedPromptShown[i].load(std::memory_order_acquire) ||
           (requestedMask & (1u << i)) != 0)) {
        if (monitor) *monitor = i;
        return true;
      }
    }
  }
  return false;
}

uint16_t flightBatteryPromptPackMask(uint8_t monitor)
{
  if (monitor >= MAX_BATTERY_MONITORS) return 0;
  return s_flightBatteryPublishedPromptMask[monitor].load(std::memory_order_acquire);
}

bool flightBatteryPromptAllowsManual(uint8_t monitor)
{
  if (monitor >= MAX_BATTERY_MONITORS) return false;
  BatteryMonitorData& config = g_model.batteryMonitors[monitor];
  return isValidManualLipoConfig(config) &&
         s_flightBatteryPublishedPromptMask[monitor].load(std::memory_order_acquire) == 0;
}

void requestFlightBatteryPrompt(uint8_t monitor)
{
  if (monitor >= MAX_BATTERY_MONITORS) return;
  s_flightBatteryPromptRequestMask.fetch_or(uint16_t(1u << monitor),
                                            std::memory_order_acq_rel);
}

void requestFlightBatteryBlockedPrompt()
{
  uint16_t requestMask = 0;
  for (uint8_t i = 0; i < MAX_BATTERY_MONITORS; i++) {
    if (publishedFlightBatteryState(i) ==
        FlightBatterySessionState::NeedsConfirmation) {
      requestMask |= uint16_t(1u << i);
    }
  }
  if (requestMask != 0) {
    s_flightBatteryPromptRequestMask.fetch_or(requestMask,
                                              std::memory_order_acq_rel);
  }
}

void markFlightBatteryPromptShown(uint8_t monitor)
{
  if (monitor >= MAX_BATTERY_MONITORS) return;

  TelemetryDataLock telemetryLock;
  flightBatteryRuntimeState[monitor].promptShown = true;
  publishFlightBatteryRuntimeState(monitor);
  s_flightBatteryPromptRequestMask.fetch_and(uint16_t(~(1u << monitor)),
                                             std::memory_order_acq_rel);
}

bool confirmFlightBatteryPack(uint8_t monitor, uint8_t selectedPackSlot)
{
  if (monitor >= MAX_BATTERY_MONITORS) return false;

  TelemetryDataLock telemetryLock;

  FlightBatteryRuntimeState& runtime = flightBatteryRuntimeState[monitor];
  BatteryMonitorData& config = g_model.batteryMonitors[monitor];
  uint16_t packVoltageCv = 0;
  if (runtime.state != FlightBatterySessionState::NeedsConfirmation ||
      !readFlightBatteryVoltage(config, packVoltageCv) ||
      packVoltageCv < FLIGHT_BATTERY_NO_BATTERY_MAX_CV) {
    return false;
  }

  if (selectedPackSlot > 0) {
    const uint8_t slot = selectedPackSlot - 1;
    if (slot >= MAX_BATTERY_PACKS ||
        (runtime.promptPackMask & (1u << slot)) == 0) {
      return false;
    }

    const BatteryPackData& pack = g_eeGeneral.batteryPacks[slot];
    if (!pack.active ||
        !flightBatteryPackMatchesLipo(packVoltageCv, pack.cellCount)) {
      return false;
    }
  } else if (!isValidManualLipoConfig(config) ||
             !flightBatteryPackMatchesLipo(packVoltageCv, config.cellCount)) {
    return false;
  }

  confirmFlightBatteryPackImpl(monitor, selectedPackSlot);
  publishFlightBatteryRuntimeState(monitor);
  return true;
}

bool checkFlightBatteryCapacityAlert(uint8_t monitorIndex,
                                     const BatteryMonitorData& config,
                                     int32_t consumed)
{
  if (!config.capAlertEnabled || config.capacity <= 0) return false;

  auto& runtime = flightBatteryRuntimeState[monitorIndex];
  if (runtime.state != FlightBatterySessionState::Confirmed &&
      runtime.state != FlightBatterySessionState::ConfirmedWaitingForVoltage) {
    return false;
  }

  int32_t sessionConsumed = consumed;
  if (runtime.consumedBaselineMah > 0 && consumed > runtime.consumedBaselineMah) {
    sessionConsumed = consumed - runtime.consumedBaselineMah;
  } else if (runtime.consumedBaselineMah > 0) {
    sessionConsumed = consumed;
  }

  if (sessionConsumed <= 0) return false;

  for (size_t i = 0; i < FLIGHT_BATTERY_CAPACITY_THRESHOLDS_SIZE; i++) {
    const uint8_t threshold = FLIGHT_BATTERY_CAPACITY_THRESHOLDS[i];
    if (!(runtime.capacityMask & (1 << i)) &&
        flightBatteryCapacityThresholdReached(sessionConsumed, config.capacity,
                                              threshold)) {
      runtime.capacityMask |= (1 << i);
      playNumber(100 - threshold, UNIT_PERCENT, 0, 0);
      return true;
    }
  }
  return false;
}

static bool checkFlightBatteryVoltageAlert(uint8_t monitorIndex,
                                           const BatteryMonitorData& config)
{
  if (!config.voltAlertEnabled) return false;

  auto& runtime = flightBatteryRuntimeState[monitorIndex];
  if (runtime.state != FlightBatterySessionState::Confirmed &&
      runtime.state != FlightBatterySessionState::ConfirmedWaitingForVoltage) {
    return false;
  }

  if (runtime.voltageAlerted) return false;

  uint16_t packVoltageCv = 0;
  if (!readFlightBatteryVoltage(config, packVoltageCv)) return false;

  uint8_t cellCount = config.cellCount;
  if (runtime.confirmedPackSlot > 0) {
    const uint8_t slot = runtime.confirmedPackSlot - 1;
    if (slot >= MAX_BATTERY_PACKS || !g_eeGeneral.batteryPacks[slot].active)
      return false;
    cellCount = g_eeGeneral.batteryPacks[slot].cellCount;
  }

  if (cellCount == 0) return false;

  const int capSensorIdx = findFlightBatteryCapacitySensor(config);
  const bool capacityUsable = capSensorIdx >= 0 && isUsableTelemetrySensor(capSensorIdx);
  const uint16_t thresholdPerCell = capacityUsable
                                        ? FLIGHT_BATTERY_LIPO_BACKUP_MIN_PER_CELL_CV
                                        : flightBatteryVoltageThresholdPerCellCentivolts(BATTERY_TYPE_LIPO);

  if (packVoltageCv <= uint16_t(thresholdPerCell * cellCount)) {
    if (runtime.voltageLowSeconds < FLIGHT_BATTERY_VOLTAGE_DEBOUNCE_SECONDS)
      runtime.voltageLowSeconds++;
  } else {
    runtime.voltageLowSeconds = 0;
  }

  if (runtime.voltageLowSeconds < FLIGHT_BATTERY_VOLTAGE_DEBOUNCE_SECONDS)
    return false;

  runtime.voltageAlerted = true;
  const uint16_t cellVoltageCv = uint16_t(divRoundClosest(packVoltageCv, cellCount));
  playNumber(uint16_t((cellVoltageCv + 5) / 10), UNIT_VOLTS, PREC1, 0);
  return true;
}

static bool checkFlightBatteryAlerts()
{
  for (uint8_t i = 0; i < MAX_BATTERY_MONITORS; i++) {
    const auto& config = g_model.batteryMonitors[i];
    if (!config.enabled) continue;

    const int capSensorIdx = findFlightBatteryCapacitySensor(config);
    if (capSensorIdx >= 0 && isUsableTelemetrySensor(capSensorIdx)) {
      const int32_t consumed = telemetrySensorValue(capSensorIdx, UNIT_MAH, 0);
      if (checkFlightBatteryCapacityAlert(i, config, consumed)) return true;
    }

    if (checkFlightBatteryVoltageAlert(i, config)) return true;
  }

  return false;
}

static void checkFlightBatteryMissingTelemetryAfterArming()
{
  constexpr uint8_t MISSING_TELEMETRY_ARMED_WARNING_SECONDS = 10;
  static bool wasArmed = false;
  static bool warningPlayed = false;
  static uint8_t armedSeconds = 0;

  const bool armed = isModelArmedState();
  if (!armed) {
    wasArmed = false;
    warningPlayed = false;
    armedSeconds = 0;
    return;
  }

  if (!wasArmed) {
    wasArmed = true;
    warningPlayed = false;
    armedSeconds = 0;
  } else if (armedSeconds < MISSING_TELEMETRY_ARMED_WARNING_SECONDS) {
    armedSeconds++;
  }

  if (warningPlayed || armedSeconds < MISSING_TELEMETRY_ARMED_WARNING_SECONDS) {
    return;
  }

  for (uint8_t i = 0; i < MAX_BATTERY_MONITORS; i++) {
    if (hasMissingArmedFlightBatteryTelemetry(g_model.batteryMonitors[i])) {
      AUDIO_WARNING1();
      warningPlayed = true;
      return;
    }
  }
}

bool flightBatteryArmingAllowed()
{
  return s_flightBatteryPublishedArmingAllowed.load(std::memory_order_acquire) != 0;
}

ArmingBlockReason flightBatteryArmingBlockReason()
{
  return ArmingBlockReason(s_flightBatteryPublishedBlockReason.load(
      std::memory_order_acquire));
}
