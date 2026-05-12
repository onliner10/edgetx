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
#include "hal/module_port.h"
#include "pulses/afhds2.h"
#include "pulses/afhds3.h"
#include "pulses/crossfire.h"
#include "pulses/dsm2.h"
#include "pulses/dsmp.h"
#include "pulses/ghost.h"
#include "pulses/multi.h"
#include "pulses/ppm.h"
#include "pulses/pulses.h"
#include "pulses/pxx1.h"
#include "pulses/pxx2.h"
#include "pulses/sbus.h"
#include "simulib.h"
#include "storage/storage.h"
#include "telemetry/frsky.h"
#include "telemetry/telemetry.h"
#include "telemetry/telemetry_sensors.h"

extern int32_t lastAct;
int32_t lastAct = 0;

uint16_t simuGetAnalog(uint8_t) { return 0; }
void simuQueueAudio(const uint8_t*, uint32_t) {}
void simuTrace(const char*) {}
void simuLcdNotify() {}

namespace {

class FuzzInput {
 public:
  FuzzInput(const uint8_t* data, size_t size) : data(data), size(size) {}

  uint8_t byte(uint8_t fallback = 0)
  {
    if (offset >= size) return fallback;
    return data[offset++];
  }

  template <typename T>
  void fill(T& value)
  {
    value = {};
    fillBytes(&value, sizeof(T));
  }

  void fillBytes(void* target, size_t targetSize)
  {
    const size_t bytes = std::min(targetSize, remaining());
    if (bytes > 0) {
      memcpy(target, data + offset, bytes);
      offset += bytes;
    }
  }

  size_t remaining() const { return size - offset; }

 private:
  const uint8_t* data;
  size_t size;
  size_t offset = 0;
};

void resetProtocolState()
{
  generalDefault();
  memset(&g_model, 0, sizeof(g_model));
  telemetryReset();
  allowNewSensors = true;
  modulePortInit();
  pulsesInit();
  outputTelemetryBuffer.reset();
  storageDirtyMsk = 0;
  for (uint8_t i = 0; i < NUM_MODULES; i++) {
    moduleState[i].protocol = PROTOCOL_CHANNELS_NONE;
    moduleState[i].mode = MODULE_MODE_NORMAL;
    moduleState[i].forced_off = 0;
    moduleState[i].settings_updated = 0;
    moduleState[i].counter = 0;
  }
}

void fillChannels(FuzzInput& input, int16_t* channels, size_t count)
{
  for (size_t i = 0; i < count; i++) {
    int16_t value = 0;
    input.fill(value);
    channels[i] = limit<int16_t>(-1024, value, 1024);
  }
}

void configureBasicModuleData(FuzzInput& input, uint8_t module, uint8_t type)
{
  g_model.header.modelId[module] = input.byte();
  g_model.moduleData[module].type = type;
  g_model.moduleData[module].subType = input.byte();
  g_model.moduleData[module].channelsStart = input.byte() % MAX_OUTPUT_CHANNELS;
  g_model.moduleData[module].channelsCount =
      static_cast<int8_t>(input.byte());
  g_model.moduleData[module].failsafeMode = input.byte();
  moduleState[module].mode = MODULE_MODE_NORMAL;
}

void fuzzProtoDriver(FuzzInput& input, const etx_proto_driver_t& driver,
                     uint8_t module)
{
  auto ctx = driver.init(module);
  if (!ctx) return;

  uint8_t rxBuffer[TELEMETRY_RX_PACKET_SIZE] = {};
  uint8_t len = input.byte() % TELEMETRY_RX_PACKET_SIZE;
  input.fillBytes(rxBuffer, len);

  if (driver.processData) {
    const uint8_t rxBytes = input.byte(48);
    for (uint8_t i = 0; i < rxBytes && input.remaining() > 0; i++) {
      driver.processData(ctx, input.byte(), rxBuffer, &len);
    }
  }

  if (driver.processFrame) {
    uint8_t frame[TELEMETRY_RX_PACKET_SIZE] = {};
    const uint8_t frameLen = input.byte() % TELEMETRY_RX_PACKET_SIZE;
    input.fillBytes(frame, frameLen);
    driver.processFrame(ctx, frame, frameLen, rxBuffer, &len);
  }

  int16_t channels[MAX_OUTPUT_CHANNELS] = {};
  fillChannels(input, channels, DIM(channels));

  uint8_t txBuffer[MODULE_BUFFER_SIZE] = {};
  const uint8_t pulseFrames = 1 + (input.byte() % 8);
  for (uint8_t i = 0; i < pulseFrames; i++) {
    driver.sendPulses(ctx, txBuffer, channels, DIM(channels));
    if (driver.txCompleted) {
      (void)driver.txCompleted(ctx);
    }
  }

  if (driver.onConfigChange && (input.byte() & 1)) {
    driver.onConfigChange(ctx);
  }

  driver.deinit(ctx);
}

void fuzzDsmpTelemetry(FuzzInput& input)
{
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_LEMON_DSMP;
  g_model.moduleData[EXTERNAL_MODULE].channelsStart =
      input.byte() % MAX_OUTPUT_CHANNELS;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount =
      static_cast<int8_t>(input.byte());
  g_model.moduleData[EXTERNAL_MODULE].dsmp.flags = input.byte();
  g_model.moduleData[EXTERNAL_MODULE].dsmp.enableAETR = input.byte() & 1;
  moduleState[EXTERNAL_MODULE].mode =
      (input.byte() & 1) ? MODULE_MODE_BIND : MODULE_MODE_NORMAL;

  auto ctx = DSMPDriver.init(EXTERNAL_MODULE);
  if (!ctx) return;

  uint8_t rxBuffer[TELEMETRY_RX_PACKET_SIZE] = {};
  uint8_t len = input.byte() % TELEMETRY_RX_PACKET_SIZE;
  input.fillBytes(rxBuffer, len);

  const uint8_t bytesToProcess = input.byte(32);
  for (uint8_t i = 0; i < bytesToProcess && input.remaining() > 0; i++) {
    DSMPDriver.processData(ctx, input.byte(), rxBuffer, &len);
  }

  int16_t channels[MAX_OUTPUT_CHANNELS] = {};
  fillChannels(input, channels, DIM(channels));

  uint8_t txBuffer[MODULE_BUFFER_SIZE] = {};
  const uint8_t pulseFrames = 1 + (input.byte() % 8);
  for (uint8_t i = 0; i < pulseFrames; i++) {
    DSMPDriver.sendPulses(ctx, txBuffer, channels, DIM(channels));
  }

  DSMPDriver.deinit(ctx);
}

void fuzzDsm2Pulses(FuzzInput& input)
{
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_DSM2;
  g_model.moduleData[EXTERNAL_MODULE].subType = input.byte();
  g_model.moduleData[EXTERNAL_MODULE].channelsStart =
      input.byte() % MAX_OUTPUT_CHANNELS;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount =
      static_cast<int8_t>(input.byte());

  auto ctx = DSM2Driver.init(EXTERNAL_MODULE);
  if (!ctx) return;

  int16_t channels[MAX_OUTPUT_CHANNELS] = {};
  fillChannels(input, channels, DIM(channels));

  uint8_t txBuffer[MODULE_BUFFER_SIZE] = {};
  const uint8_t pulseFrames = 1 + (input.byte() % 8);
  for (uint8_t i = 0; i < pulseFrames; i++) {
    DSM2Driver.sendPulses(ctx, txBuffer, channels, DIM(channels));
  }

  DSM2Driver.deinit(ctx);
}

void setFuzzModuleMode(FuzzInput& input, uint8_t module)
{
  switch (input.byte() % 3) {
    case 0:
      moduleState[module].mode = MODULE_MODE_NORMAL;
      break;
    case 1:
      moduleState[module].mode = MODULE_MODE_BIND;
      break;
    default:
      moduleState[module].mode = MODULE_MODE_RANGECHECK;
      break;
  }
}

void configureFrSkyModuleData(FuzzInput& input, uint8_t module)
{
  g_model.header.modelId[module] = input.byte();
  g_model.moduleData[module].channelsStart = input.byte() % MAX_OUTPUT_CHANNELS;
  g_model.moduleData[module].channelsCount =
      static_cast<int8_t>(input.byte());
  g_model.moduleData[module].failsafeMode = input.byte();
  g_model.moduleData[module].pxx.receiverTelemetryOff = input.byte() & 1;
  g_model.moduleData[module].pxx.receiverHigherChannels = input.byte() & 1;
  g_model.moduleData[module].pxx.power = input.byte();
  g_model.moduleData[module].pxx.antennaMode = input.byte();
  setFuzzModuleMode(input, module);
}

#if defined(PXX1)
void fuzzFrSkyPxx1(FuzzInput& input)
{
  const uint8_t module = EXTERNAL_MODULE;
  configureFrSkyModuleData(input, module);

  switch (input.byte() % 3) {
    case 0:
      g_model.moduleData[module].type = MODULE_TYPE_XJT_PXX1;
      g_model.moduleData[module].subType =
          input.byte() % (MODULE_SUBTYPE_PXX1_LAST + 1);
      break;
    case 1:
      g_model.moduleData[module].type = MODULE_TYPE_R9M_PXX1;
      g_model.moduleData[module].subType =
          input.byte() % (MODULE_SUBTYPE_R9M_LAST + 1);
      break;
    default:
      g_model.moduleData[module].type = MODULE_TYPE_R9M_LITE_PXX1;
      g_model.moduleData[module].subType =
          input.byte() % (MODULE_SUBTYPE_R9M_LAST + 1);
      break;
  }

  auto ctx = Pxx1Driver.init(module);
  if (!ctx) return;

  uint8_t rxBuffer[TELEMETRY_RX_PACKET_SIZE] = {};
  uint8_t len = input.byte() % TELEMETRY_RX_PACKET_SIZE;
  input.fillBytes(rxBuffer, len);
  const uint8_t rxBytes = input.byte(32);
  for (uint8_t i = 0; i < rxBytes && input.remaining() > 0; i++) {
    Pxx1Driver.processData(ctx, input.byte(), rxBuffer, &len);
  }

  int16_t channels[MAX_OUTPUT_CHANNELS] = {};
  fillChannels(input, channels, DIM(channels));

  uint8_t txBuffer[MODULE_BUFFER_SIZE] = {};
  const uint8_t pulseFrames = 1 + (input.byte() % 8);
  for (uint8_t i = 0; i < pulseFrames; i++) {
    Pxx1Driver.sendPulses(ctx, txBuffer, channels, DIM(channels));
  }

  Pxx1Driver.deinit(ctx);
}
#endif

#if defined(PXX2)
void fuzzFrSkyPxx2(FuzzInput& input)
{
  const bool useInternal = input.byte() & 1;
  const uint8_t module = useInternal ? INTERNAL_MODULE : EXTERNAL_MODULE;
  configureFrSkyModuleData(input, module);

  if (module == INTERNAL_MODULE) {
    g_eeGeneral.internalModule = MODULE_TYPE_ISRM_PXX2;
    g_model.moduleData[module].type = MODULE_TYPE_ISRM_PXX2;
    g_model.moduleData[module].subType =
        input.byte() % (MODULE_SUBTYPE_ISRM_PXX2_LAST + 1);
  }
  else {
    switch (input.byte() % 4) {
      case 0:
        g_model.moduleData[module].type = MODULE_TYPE_ISRM_PXX2;
        g_model.moduleData[module].subType =
            input.byte() % (MODULE_SUBTYPE_ISRM_PXX2_LAST + 1);
        break;
      case 1:
        g_model.moduleData[module].type = MODULE_TYPE_R9M_PXX2;
        g_model.moduleData[module].subType =
            input.byte() % (MODULE_SUBTYPE_R9M_LAST + 1);
        break;
      case 2:
        g_model.moduleData[module].type = MODULE_TYPE_R9M_LITE_PXX2;
        g_model.moduleData[module].subType =
            input.byte() % (MODULE_SUBTYPE_R9M_LAST + 1);
        break;
      default:
        g_model.moduleData[module].type = MODULE_TYPE_XJT_LITE_PXX2;
        g_model.moduleData[module].subType =
            input.byte() % (MODULE_SUBTYPE_ISRM_PXX2_LAST + 1);
        break;
    }
  }

  auto ctx = Pxx2Driver.init(module);
  if (!ctx) return;

  uint8_t rxBuffer[TELEMETRY_RX_PACKET_SIZE] = {};
  uint8_t len = input.byte() % TELEMETRY_RX_PACKET_SIZE;
  input.fillBytes(rxBuffer, len);
  const uint8_t rxBytes = input.byte(40);
  for (uint8_t i = 0; i < rxBytes && input.remaining() > 0; i++) {
    Pxx2Driver.processData(ctx, input.byte(), rxBuffer, &len);
  }

  int16_t channels[MAX_OUTPUT_CHANNELS] = {};
  fillChannels(input, channels, DIM(channels));

  uint8_t txBuffer[MODULE_BUFFER_SIZE] = {};
  const uint8_t pulseFrames = 1 + (input.byte() % 8);
  for (uint8_t i = 0; i < pulseFrames; i++) {
    Pxx2Driver.sendPulses(ctx, txBuffer, channels, DIM(channels));
  }

  Pxx2Driver.deinit(ctx);
}
#endif

void fuzzFrSkyTelemetry(FuzzInput& input)
{
  uint8_t frskyDBuffer[TELEMETRY_RX_PACKET_SIZE] = {};
  uint8_t frskyDLen = input.byte() % TELEMETRY_RX_PACKET_SIZE;
  input.fillBytes(frskyDBuffer, frskyDLen);

  uint8_t sportBuffer[TELEMETRY_RX_PACKET_SIZE] = {};
  uint8_t sportLen = input.byte() % TELEMETRY_RX_PACKET_SIZE;
  input.fillBytes(sportBuffer, sportLen);

  const uint8_t bytesToProcess = input.byte(64);
  for (uint8_t i = 0; i < bytesToProcess && input.remaining() > 0; i++) {
    uint8_t data = input.byte();
    if (input.byte() & 1) {
      processFrskyDTelemetryData(EXTERNAL_MODULE, data, frskyDBuffer,
                                 &frskyDLen);
    }
    else {
      processFrskySportTelemetryData(EXTERNAL_MODULE, data, sportBuffer,
                                     &sportLen);
    }
  }

  uint8_t directPacket[TELEMETRY_RX_PACKET_SIZE] = {};
  const uint8_t directLen = input.byte() % TELEMETRY_RX_PACKET_SIZE;
  input.fillBytes(directPacket, directLen);
  frskyDProcessPacket(EXTERNAL_MODULE, directPacket, directLen);
}

void fuzzPxx1SportPayload(FuzzInput& input)
{
  outputTelemetryBuffer.reset();
  outputTelemetryBuffer.setDestination(TELEMETRY_ENDPOINT_SPORT);
  outputTelemetryBuffer.size = input.byte();
  outputTelemetryBuffer.sport.physicalId = input.byte();
  input.fillBytes(outputTelemetryBuffer.data, TELEMETRY_OUTPUT_BUFFER_SIZE);

  uint8_t payloadSize = 0;
  (void)pxx1PrepareSportTelemetryPayloadForTest(payloadSize);
  outputTelemetryBuffer.reset();
}

void fuzzProtocolInitRetry(FuzzInput& input)
{
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_LEMON_DSMP;
  pulsesForceModuleInitFailureForTest(EXTERNAL_MODULE, PROTOCOL_CHANNELS_DSMP);

  const uint8_t iterations = 1 + (input.byte() % 16);
  for (uint8_t i = 0; i < iterations; i++) {
    if (input.byte() & 1) {
      pulsesModuleSettingsUpdate(EXTERNAL_MODULE);
    }
    pulsesSendNextFrame(EXTERNAL_MODULE);
  }

  pulsesForceModuleInitFailureForTest(EXTERNAL_MODULE,
                                      PROTOCOL_CHANNELS_UNINITIALIZED);
}

void fuzzCrsfGhostMulti(FuzzInput& input)
{
  const uint8_t module = EXTERNAL_MODULE;
  switch (input.byte() % 3) {
    case 0:
      configureBasicModuleData(input, module, MODULE_TYPE_CROSSFIRE);
      g_model.moduleData[module].crsf.telemetryBaudrate = input.byte();
      g_model.moduleData[module].crsf.crsfArmingMode = input.byte() & 1;
      g_model.moduleData[module].crsf.crsfArmingTrigger =
          static_cast<int16_t>(input.byte());
      fuzzProtoDriver(input, CrossfireDriver, module);
      break;
    case 1:
      configureBasicModuleData(input, module, MODULE_TYPE_GHOST);
      g_model.moduleData[module].ghost.raw12bits = input.byte() & 1;
      g_model.moduleData[module].ghost.telemetryBaudrate = input.byte();
      fuzzProtoDriver(input, GhostDriver, module);
      break;
    default:
#if defined(MULTIMODULE)
      configureBasicModuleData(input, module, MODULE_TYPE_MULTIMODULE);
      g_model.moduleData[module].multi.rfProtocol = input.byte();
      g_model.moduleData[module].multi.optionValue =
          static_cast<int8_t>(input.byte());
      g_model.moduleData[module].multi.autoBindMode = input.byte() & 1;
      g_model.moduleData[module].multi.lowPowerMode = input.byte() & 1;
      fuzzProtoDriver(input, MultiDriver, module);
#endif
      break;
  }
}

void fuzzPpmSbusFlySky(FuzzInput& input)
{
  const uint8_t module = EXTERNAL_MODULE;
  switch (input.byte() % 4) {
    case 0:
      configureBasicModuleData(input, module, MODULE_TYPE_PPM);
      g_model.moduleData[module].ppm.delay = input.byte();
      g_model.moduleData[module].ppm.pulsePol = input.byte() & 1;
      g_model.moduleData[module].ppm.outputType = input.byte() & 1;
      g_model.moduleData[module].ppm.frameLength =
          static_cast<int8_t>(input.byte());
      fuzzProtoDriver(input, PpmDriver, module);
      break;
    case 1:
      configureBasicModuleData(input, module, MODULE_TYPE_SBUS);
      g_model.moduleData[module].sbus.noninverted = input.byte() & 1;
      g_model.moduleData[module].sbus.refreshRate =
          static_cast<int8_t>(input.byte());
      fuzzProtoDriver(input, SBusDriver, module);
      break;
    case 2:
#if defined(AFHDS2)
      configureBasicModuleData(input, INTERNAL_MODULE,
                               MODULE_TYPE_FLYSKY_AFHDS2A);
      g_model.moduleData[INTERNAL_MODULE].flysky.mode = input.byte();
      g_model.moduleData[INTERNAL_MODULE].flysky.rfPower = input.byte() & 1;
      input.fillBytes(g_model.moduleData[INTERNAL_MODULE].flysky.rx_id,
                      sizeof(g_model.moduleData[INTERNAL_MODULE].flysky.rx_id));
      fuzzProtoDriver(input, Afhds2InternalDriver, INTERNAL_MODULE);
#endif
      break;
    default:
#if defined(AFHDS3)
      configureBasicModuleData(input, module, MODULE_TYPE_FLYSKY_AFHDS3);
      g_model.moduleData[module].afhds3.emi = input.byte();
      g_model.moduleData[module].afhds3.telemetry = input.byte() & 1;
      g_model.moduleData[module].afhds3.phyMode = input.byte();
      g_model.moduleData[module].afhds3.rfPower = input.byte();
      fuzzProtoDriver(input, afhds3::ProtoDriver, module);
#endif
      break;
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  static bool initialized = false;
  if (!initialized) {
    simuInit();
    initialized = true;
  }

  FuzzInput input(data, size);
  resetProtocolState();

  switch (input.byte() % 9) {
    case 0:
      fuzzDsmpTelemetry(input);
      break;
    case 1:
      fuzzDsm2Pulses(input);
      break;
    case 2:
      fuzzFrSkyTelemetry(input);
      break;
    case 3:
#if defined(PXX1)
      fuzzFrSkyPxx1(input);
#endif
      break;
    case 4:
#if defined(PXX2)
      fuzzFrSkyPxx2(input);
#endif
      break;
    case 5:
      fuzzPxx1SportPayload(input);
      break;
    case 6:
      fuzzCrsfGhostMulti(input);
      break;
    case 7:
      fuzzPpmSbusFlySky(input);
      break;
    default:
      fuzzProtocolInitRetry(input);
      break;
  }

  return 0;
}
