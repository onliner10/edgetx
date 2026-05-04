/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "edgetx.h"
#include "hal/adc_driver.h"
#include "hal/switch_driver.h"
#include "model_init.h"
#include "simulib.h"
#include "switches.h"

extern int32_t lastAct;
int32_t lastAct = 0;

uint16_t simuGetAnalog(uint8_t) { return 0; }
void simuQueueAudio(const uint8_t *, uint32_t) {}
void simuTrace(const char*) {}
void simuLcdNotify() {}

extern void anaResetFiltered();
extern void anaSetFiltered(uint8_t chan, uint16_t val);

namespace {

class FuzzInput {
 public:
  FuzzInput(const uint8_t *data, size_t size) : data(data), size(size) {}

  template <typename T>
  void fill(T &value)
  {
    value = {};
    fillBytes(&value, sizeof(T));
  }

  template <typename T, size_t N>
  void fill(T (&value)[N])
  {
    memset(value, 0, sizeof(value));
    fillBytes(value, sizeof(value));
  }

  void fillBytes(void *target, size_t targetSize)
  {
    const size_t bytes = std::min(targetSize, remaining());
    if (bytes > 0) {
      memcpy(target, data + offset, bytes);
      offset += bytes;
    }
  }

  uint8_t byte(uint8_t fallback = 0)
  {
    if (remaining() == 0) return fallback;
    return data[offset++];
  }

  int16_t analog()
  {
    uint16_t raw = 0;
    fill(raw);
    return static_cast<int16_t>((raw % 2049) - 1024);
  }

  size_t remaining() const { return size - offset; }

 private:
  const uint8_t *data;
  size_t size;
  size_t offset = 0;
};

void resetRadioState()
{
#if !defined(STORAGE_MODELSLIST)
  memset(modelHeaders, 0, sizeof(modelHeaders));
#endif
  generalDefault();
  g_eeGeneral.templateSetup = 0;
  for (int i = 0; i < switchGetMaxAllSwitches(); i++) {
    simuSetSwitch(i, -1);
  }

  memset(&g_model, 0, sizeof(g_model));
  anaResetFiltered();
  s_mixer_first_run_done = false;
  evalMixes(1);
  lastFlightMode = 255;

  memset(channelOutputs, 0, sizeof(channelOutputs));
  memset(chans, 0, sizeof(chans));
  memset(ex_chans, 0, sizeof(ex_chans));
  memset(act, 0, sizeof(act));
  memset(mixState, 0, sizeof(mixState));
  mixerCurrentFlightMode = lastFlightMode = 0;
  lastAct = 0;
  logicalSwitchesReset();

  setModelDefaults();
  for (int i = 0; i < switchGetMaxAllSwitches(); i++) {
    g_eeGeneral.switchSetType(i, i == 5 ? SWITCH_2POS : SWITCH_3POS);
  }
}

void normalizeModel()
{
  for (auto &mix : g_model.mixData) {
    mix.destCh %= MAX_OUTPUT_CHANNELS;
    if (mix.srcRaw > MIXSRC_LAST_CH) {
      mix.srcRaw = MIXSRC_NONE;
    }
    mix.swtch = 0;
    mix.flightModes &= 0x1ff;
  }

  for (auto &expo : g_model.expoData) {
    expo.chn %= MAX_INPUTS;
    if (expo.srcRaw > MIXSRC_LAST_CH) {
      expo.srcRaw = MIXSRC_NONE;
    }
    expo.swtch = 0;
    expo.flightModes &= 0x1ff;
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  static bool initialized = false;
  if (!initialized) {
    simuInit();
    initialized = true;
  }

  FuzzInput input(data, size);
  resetRadioState();

  input.fill(g_model.mixData);
  input.fill(g_model.expoData);
  input.fill(g_model.limitData);
  normalizeModel();

  for (uint8_t i = 0; i < MAX_INPUTS; i++) {
    anaSetFiltered(i, input.analog());
  }

  const uint8_t iterations = 1 + (input.byte() % 8);
  for (uint8_t i = 0; i < iterations; i++) {
    const uint8_t tick10ms = input.byte(i == 0) & 1;
    evalFlightModeMixes(e_perout_mode_normal, tick10ms);
  }

  for (uint8_t i = 0; i < MAX_OUTPUT_CHANNELS; i++) {
    (void)channelOutputs[i];
  }

  return 0;
}
