/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x
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

#include "gtests.h"

#include "hal/module_port.h"
#include "pulses/pulses.h"

#if defined(PXX2) || defined(AFHDS3)
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(MULTIMODULE)
#include "pulses/multi.h"
#include "telemetry/multi.h"
#endif

#if defined(SBUS)
#include "pulses/sbus.h"
#endif

#if defined(DSM2)
#include "pulses/dsm2.h"
#endif

#if defined(DSMP)
#include "pulses/dsmp.h"
#include "telemetry/spektrum.h"
#endif

#if defined(PXX1)
#include "pulses/pxx1.h"
#include "pulses/pxx1_transport.h"
#endif

#if defined(PXX2)
#include "pulses/pxx2.h"
#include "pulses/pxx2_transport.h"
#endif

#if defined(AFHDS3)
#include "pulses/afhds3.h"
#include "pulses/afhds3_config.h"
#include "pulses/afhds3_transport.h"
namespace afhds3 {
Config_u * getConfig(uint8_t module);
void applyModelConfig(uint8_t module);
}
#endif

#if defined(PPM)
#include "pulses/ppm.h"
#endif

extern uint8_t getRequiredProtocol(uint8_t module);
int32_t getChannelValue(uint8_t channel);

namespace {

bool sendPulsesCalled = false;

void sendPulsesReadsWhenChannelsAreExposed(void*, uint8_t*, int16_t* channels,
                                           uint8_t nChannels)
{
  sendPulsesCalled = true;
  EXPECT_EQ(channels, nullptr);
  EXPECT_EQ(nChannels, 0);

  if (channels != nullptr && nChannels > 0) {
    volatile int16_t firstChannel = channels[0];
    (void)firstChannel;
  }
}

const etx_proto_driver_t ChannelBoundsDriver = {
    .protocol = PROTOCOL_CHANNELS_NONE,
    .init = nullptr,
    .deinit = nullptr,
    .sendPulses = sendPulsesReadsWhenChannelsAreExposed,
    .processData = nullptr,
    .processFrame = nullptr,
    .onConfigChange = nullptr,
    .txCompleted = nullptr,
};

#if defined(PXX2)
class GuardedPxx2Frame
{
 public:
  explicit GuardedPxx2Frame(uint8_t declaredLength):
      mappingSize(0),
      mapping(nullptr),
      frame(nullptr)
  {
    long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0 || declaredLength + 1 > pageSize) {
      return;
    }

    mappingSize = static_cast<size_t>(pageSize * 2);
    mapping = mmap(nullptr, mappingSize, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
      mapping = nullptr;
      mappingSize = 0;
      return;
    }

    if (mprotect(static_cast<uint8_t *>(mapping) + pageSize, pageSize,
                 PROT_NONE) != 0) {
      munmap(mapping, mappingSize);
      mapping = nullptr;
      mappingSize = 0;
      return;
    }

    frame = static_cast<uint8_t *>(mapping) + pageSize - (declaredLength + 1);
    frame[0] = declaredLength;
  }

  ~GuardedPxx2Frame()
  {
    if (mapping != nullptr) {
      munmap(mapping, mappingSize);
    }
  }

  bool isValid() const
  {
    return frame != nullptr;
  }

  uint8_t * data()
  {
    return frame;
  }

 private:
  size_t mappingSize;
  void * mapping;
  uint8_t * frame;
};
#endif

#if defined(AFHDS3)
class GuardedAfhds3RxBuffer
{
 public:
  explicit GuardedAfhds3RxBuffer(size_t size):
      mappingSize(0),
      mapping(nullptr),
      frame(nullptr)
  {
    long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0 || size > static_cast<size_t>(pageSize)) {
      return;
    }

    mappingSize = static_cast<size_t>(pageSize * 2);
    mapping = mmap(nullptr, mappingSize, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
      mapping = nullptr;
      mappingSize = 0;
      return;
    }

    if (mprotect(static_cast<uint8_t *>(mapping) + pageSize, pageSize,
                 PROT_NONE) != 0) {
      munmap(mapping, mappingSize);
      mapping = nullptr;
      mappingSize = 0;
      return;
    }

    frame = static_cast<uint8_t *>(mapping) + pageSize - size;
    memset(frame, 0, size);
  }

  ~GuardedAfhds3RxBuffer()
  {
    if (mapping != nullptr) {
      munmap(mapping, mappingSize);
    }
  }

  bool isValid() const { return frame != nullptr; }

  uint8_t * data()
  {
    return frame;
  }

 private:
  size_t mappingSize;
  void * mapping;
  uint8_t * frame;
};

void feedShortAfhds3Response(void* ctx, afhds3::COMMAND command,
                             uint8_t value)
{
  uint8_t frame[8] = {0xC0,
                      0,
                      0,
                      afhds3::FRAME_TYPE::RESPONSE_DATA,
                      command,
                      value,
                      0,
                      0xC0};
  uint8_t crc = 0;
  for (uint8_t i = 1; i < 6; i++) {
    crc += frame[i];
  }
  frame[6] = crc ^ 0xff;

  uint8_t rxBuffer[8] = {};
  uint8_t len = 0;
  for (uint8_t byte : frame) {
    afhds3::ProtoDriver.processData(ctx, byte, rxBuffer, &len);
  }
}
#endif

}  // namespace

class PulsesTest : public EdgeTxTest
{
};

TEST_F(PulsesTest, invalidChannelStartDoesNotExposeOutputBuffer)
{
  pulsesInit();

  constexpr uint8_t module = EXTERNAL_MODULE;
  g_model.moduleData[module].type = MODULE_TYPE_PPM;
  g_model.moduleData[module].channelsStart = 255;
  moduleState[module].protocol = getRequiredProtocol(module);

  auto mod = pulsesGetModuleDriver(module);
  mod->drv = &ChannelBoundsDriver;
  mod->ctx = nullptr;
  sendPulsesCalled = false;

  pulsesSendNextFrame(module);

  EXPECT_TRUE(sendPulsesCalled);
  memset(mod, 0, sizeof(*mod));
}

TEST_F(PulsesTest, getChannelValueRejectsInvalidChannel)
{
  EXPECT_EQ(getChannelValue(MAX_OUTPUT_CHANNELS), 0);
}

TEST_F(PulsesTest, setCustomFailsafeRejectsInvalidModuleType)
{
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_COUNT + 1;
  g_model.moduleData[EXTERNAL_MODULE].channelsStart = 0;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount = 0;
  channelOutputs[0] = 1024;

  setCustomFailsafe(EXTERNAL_MODULE);

  EXPECT_EQ(g_model.failsafeChannels[0], 0);
}

#if defined(MULTIMODULE) && defined(HARDWARE_EXTERNAL_MODULE)
TEST_F(PulsesTest, multiSendPulsesHonorsChannelCount)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].failsafeMode = FAILSAFE_NOT_SET;
  channelOutputs[0] = 1024;

  auto ctx = MultiDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  MultiDriver.sendPulses(ctx, buffer, nullptr, 0);

  uint16_t firstPulse = buffer[4] | ((buffer[5] & 0x07) << 8);
  EXPECT_EQ(firstPulse, 1024);

  MultiDriver.deinit(ctx);
}

TEST_F(PulsesTest, multiFailsafeHonorsChannelStart)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].channelsStart = 4;
  g_model.moduleData[EXTERNAL_MODULE].failsafeMode = FAILSAFE_CUSTOM;
  g_model.failsafeChannels[0] = 1024;
  g_model.failsafeChannels[4] = -1024;

  auto ctx = MultiDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  bool failsafeSent = false;
  for (int i = 0; i <= 1000; i++) {
    memset(buffer, 0, sizeof(buffer));
    MultiDriver.sendPulses(ctx, buffer, nullptr, 0);
    if (buffer[0] & 0x02) {
      failsafeSent = true;
      break;
    }
  }

  ASSERT_TRUE(failsafeSent);
  uint16_t firstPulse = buffer[4] | ((buffer[5] & 0x07) << 8);
  EXPECT_EQ(firstPulse, 205);

  MultiDriver.deinit(ctx);
}

TEST_F(PulsesTest, multiSendPulsesRejectsInvalidRfProtocol)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_MULTIMODULE;
  g_model.moduleData[EXTERNAL_MODULE].multi.rfProtocol = 200;
  g_model.moduleData[EXTERNAL_MODULE].multi.autoBindMode = 0;
  g_model.moduleData[EXTERNAL_MODULE].failsafeMode = FAILSAFE_NOT_SET;

  auto ctx = MultiDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  MultiDriver.sendPulses(ctx, buffer, nullptr, 0);

  EXPECT_EQ(buffer[1] & 0x1f, MODULE_SUBTYPE_MULTI_FIRST + 1);
  EXPECT_EQ(buffer[26] & 0xc0, 0);

  MultiDriver.deinit(ctx);
}

TEST_F(PulsesTest, multiSendPulsesRejectsInvalidSubtype)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_MULTIMODULE;
  g_model.moduleData[EXTERNAL_MODULE].multi.rfProtocol =
      MODULE_SUBTYPE_MULTI_FRSKYX;
  g_model.moduleData[EXTERNAL_MODULE].subType = 15;
  g_model.moduleData[EXTERNAL_MODULE].failsafeMode = FAILSAFE_NOT_SET;

  auto ctx = MultiDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  MultiDriver.sendPulses(ctx, buffer, nullptr, 0);

  EXPECT_EQ(buffer[2] & 0x70, 0);

  MultiDriver.deinit(ctx);
}

TEST_F(PulsesTest, multiDsmBindPacketIgnoredOutsideBindMode)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_MULTIMODULE;
  g_model.moduleData[EXTERNAL_MODULE].multi.rfProtocol =
      MODULE_SUBTYPE_MULTI_DSM2;
  g_model.moduleData[EXTERNAL_MODULE].subType = MM_RF_DSM2_SUBTYPE_AUTO;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount = 0;
  moduleState[EXTERNAL_MODULE].mode = MODULE_MODE_NORMAL;

  const uint8_t bindFrame[] = {
      'M', 'P', 0x05, 10, 0x00, 0x00, 0x00,
      0x00, 0x00, 12,   0xb2, 0x00, 0x00, 0x00};
  for (uint8_t byte : bindFrame) {
    processMultiTelemetryData(byte, EXTERNAL_MODULE);
  }

  EXPECT_EQ(g_model.moduleData[EXTERNAL_MODULE].subType,
            MM_RF_DSM2_SUBTYPE_AUTO);
  EXPECT_EQ(g_model.moduleData[EXTERNAL_MODULE].channelsCount, 0);
  EXPECT_EQ(moduleState[EXTERNAL_MODULE].mode, MODULE_MODE_NORMAL);
}
#endif

#if defined(DSM2) && defined(HARDWARE_EXTERNAL_MODULE)
TEST_F(PulsesTest, dsm2SendPulsesHonorsChannelCount)
{
  modulePortInit();
  channelOutputs[0] = 1024;

  auto ctx = DSM2Driver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  DSM2Driver.sendPulses(ctx, buffer, nullptr, 0);

  uint16_t firstPulse = ((buffer[2] & 0x03) << 8) | buffer[3];
  EXPECT_EQ(firstPulse, 512);

  DSM2Driver.deinit(ctx);
}
#endif

#if defined(DSMP) && defined(HARDWARE_EXTERNAL_MODULE)
TEST_F(PulsesTest, dsmpSendPulsesHonorsChannelCount)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_LEMON_DSMP;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount = 0;
  g_model.moduleData[EXTERNAL_MODULE].dsmp.flags = 0;
  channelOutputs[0] = 1024;

  auto ctx = DSMPDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  DSMPDriver.sendPulses(ctx, buffer, nullptr, 0);

  uint16_t firstPulse = ((buffer[2] << 8) | buffer[3]) & 0x03FF;
  EXPECT_EQ(firstPulse, 512);

  DSMPDriver.deinit(ctx);
}

TEST_F(PulsesTest, dsmpSetupRejectsInvalidChannelCount)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_LEMON_DSMP;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount = 127;
  g_model.moduleData[EXTERNAL_MODULE].dsmp.flags = 0;

  auto ctx = DSMPDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  bool setupSent = false;
  for (int i = 0; i < 3; i++) {
    memset(buffer, 0, sizeof(buffer));
    DSMPDriver.sendPulses(ctx, buffer, nullptr, 0);
    if (buffer[0] == 0xAA && buffer[1] == 0) {
      setupSent = true;
      break;
    }
  }

  ASSERT_TRUE(setupSent);
  EXPECT_EQ(buffer[4], 12);

  DSMPDriver.deinit(ctx);
}

TEST_F(PulsesTest, dsmpBindRejectsZeroChannelCount)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_LEMON_DSMP;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount = 0;
  moduleState[EXTERNAL_MODULE].mode = MODULE_MODE_BIND;

  auto ctx = DSMPDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t rxBuffer[TELEMETRY_RX_PACKET_SIZE] = {};
  uint8_t len = 0;
  const uint8_t bindPacket[DSM_BIND_PACKET_LENGTH] = {
      0xAA, 0x80, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  for (uint8_t byte : bindPacket) {
    DSMPDriver.processData(ctx, byte, rxBuffer, &len);
  }

  EXPECT_EQ(g_model.moduleData[EXTERNAL_MODULE].channelsCount, 0);
  EXPECT_EQ(moduleState[EXTERNAL_MODULE].mode, MODULE_MODE_BIND);

  moduleState[EXTERNAL_MODULE].mode = MODULE_MODE_NORMAL;
  DSMPDriver.deinit(ctx);
}

TEST_F(PulsesTest, dsmpBindPacketIgnoredOutsideBindMode)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_LEMON_DSMP;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount = 0;
  moduleState[EXTERNAL_MODULE].mode = MODULE_MODE_NORMAL;

  auto ctx = DSMPDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t rxBuffer[TELEMETRY_RX_PACKET_SIZE] = {};
  uint8_t len = 0;
  const uint8_t bindPacket[DSM_BIND_PACKET_LENGTH] = {
      0xAA, 0x80, 0x00, 0x00, 12, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  for (uint8_t byte : bindPacket) {
    DSMPDriver.processData(ctx, byte, rxBuffer, &len);
  }

  EXPECT_EQ(g_model.moduleData[EXTERNAL_MODULE].channelsCount, 0);
  EXPECT_EQ(moduleState[EXTERNAL_MODULE].mode, MODULE_MODE_NORMAL);

  DSMPDriver.deinit(ctx);
}
#endif

#if defined(PXX1) && defined(HARDWARE_EXTERNAL_MODULE)
TEST_F(PulsesTest, pxx1SendPulsesHonorsChannelCount)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_R9M_PXX1;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount = 0;
  g_model.moduleData[EXTERNAL_MODULE].failsafeMode = FAILSAFE_NOT_SET;

  auto ctx = Pxx1Driver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t neutralFrame[MODULE_BUFFER_SIZE] = {};
  moduleState[EXTERNAL_MODULE].counter = 0;
  channelOutputs[0] = 0;
  Pxx1Driver.sendPulses(ctx, neutralFrame, nullptr, 0);

  uint8_t highFrame[MODULE_BUFFER_SIZE] = {};
  moduleState[EXTERNAL_MODULE].counter = 0;
  channelOutputs[0] = 1024;
  Pxx1Driver.sendPulses(ctx, highFrame, nullptr, 0);

  EXPECT_EQ(memcmp(neutralFrame, highFrame, sizeof(neutralFrame)), 0);

  Pxx1Driver.deinit(ctx);
}

TEST_F(PulsesTest, pxx1FailsafeHonorsChannelStart)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_R9M_PXX1;
  g_model.moduleData[EXTERNAL_MODULE].channelsStart = 4;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount = 0;
  g_model.moduleData[EXTERNAL_MODULE].failsafeMode = FAILSAFE_CUSTOM;
  g_model.failsafeChannels[4] = -1024;

  auto ctx = Pxx1Driver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t skippedChannelHighFrame[MODULE_BUFFER_SIZE] = {};
  g_model.failsafeChannels[0] = 1024;
  moduleState[EXTERNAL_MODULE].counter = 0;
  Pxx1Driver.sendPulses(ctx, skippedChannelHighFrame, nullptr, 0);

  uint8_t skippedChannelLowFrame[MODULE_BUFFER_SIZE] = {};
  g_model.failsafeChannels[0] = -1024;
  moduleState[EXTERNAL_MODULE].counter = 0;
  Pxx1Driver.sendPulses(ctx, skippedChannelLowFrame, nullptr, 0);

  EXPECT_EQ(memcmp(skippedChannelHighFrame, skippedChannelLowFrame,
                   sizeof(skippedChannelHighFrame)),
            0);

  Pxx1Driver.deinit(ctx);
}

TEST_F(PulsesTest, pxx1SendPulsesRejectsInvalidSubtype)
{
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_XJT_PXX1;
  g_model.moduleData[EXTERNAL_MODULE].subType = 15;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount = 0;
  g_model.moduleData[EXTERNAL_MODULE].failsafeMode = FAILSAFE_NOT_SET;

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  UartPxx1Pulses frame(buffer);
  frame.setupFrame(EXTERNAL_MODULE, Pxx1Type::FAST_SERIAL, nullptr, 0);

  EXPECT_EQ(buffer[2], MODULE_SUBTYPE_PXX1_ACCST_D16 << 6);
}

TEST_F(PulsesTest, pxx1R9MRejectsInvalidSubtype)
{
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_R9M_PXX1;
  g_model.moduleData[EXTERNAL_MODULE].subType = 15;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount = 0;
  g_model.moduleData[EXTERNAL_MODULE].failsafeMode = FAILSAFE_NOT_SET;

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  UartPxx1Pulses frame(buffer);
  frame.setupFrame(EXTERNAL_MODULE, Pxx1Type::FAST_SERIAL, nullptr, 0);

  EXPECT_EQ(buffer[2], MODULE_SUBTYPE_R9M_FCC << 6);
}
#endif

#if defined(PXX2) && defined(HARDWARE_INTERNAL_MODULE)
TEST_F(PulsesTest, pxx2SendPulsesHonorsChannelCount)
{
  modulePortInit();
  g_model.moduleData[INTERNAL_MODULE].type = MODULE_TYPE_ISRM_PXX2;
  g_model.moduleData[INTERNAL_MODULE].channelsCount = 0;

  auto ctx = Pxx2Driver.init(INTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  Pxx2Driver.sendPulses(ctx, buffer, nullptr, 0);

  Pxx2Driver.deinit(ctx);
}

TEST_F(PulsesTest, pxx2SendPulsesRejectsInvalidChannelCount)
{
  modulePortInit();
  g_model.moduleData[INTERNAL_MODULE].type = MODULE_TYPE_ISRM_PXX2;
  g_model.moduleData[INTERNAL_MODULE].channelsCount = 127;

  auto ctx = Pxx2Driver.init(INTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  Pxx2Driver.sendPulses(ctx, buffer, nullptr, 0);

  Pxx2Driver.deinit(ctx);
}

TEST_F(PulsesTest, pxx2IsrmRejectsInvalidSubtype)
{
  g_model.moduleData[INTERNAL_MODULE].type = MODULE_TYPE_ISRM_PXX2;
  g_model.moduleData[INTERNAL_MODULE].subType = 15;
  g_model.moduleData[INTERNAL_MODULE].channelsCount = 0;
  g_model.moduleData[INTERNAL_MODULE].failsafeMode = FAILSAFE_NOT_SET;

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  Pxx2Pulses frame(buffer);
  ASSERT_TRUE(frame.setupFrame(INTERNAL_MODULE, nullptr, 0));

  EXPECT_EQ(buffer[5], MODULE_SUBTYPE_ISRM_PXX2_ACCESS << 4);
}
#endif

#if defined(PXX2) && defined(HARDWARE_EXTERNAL_MODULE)
TEST_F(PulsesTest, pxx2XjtLiteRejectsInvalidSubtype)
{
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_XJT_LITE_PXX2;
  g_model.moduleData[EXTERNAL_MODULE].subType = 15;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount = 0;
  g_model.moduleData[EXTERNAL_MODULE].failsafeMode = FAILSAFE_NOT_SET;

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  Pxx2Pulses frame(buffer);
  ASSERT_TRUE(frame.setupFrame(EXTERNAL_MODULE, nullptr, 0));

  EXPECT_EQ(buffer[5], 0x10);
}
#endif

#if defined(PXX2)
TEST_F(PulsesTest, pxx2RejectsShortRegisterFrame)
{
  moduleState[INTERNAL_MODULE].mode = MODULE_MODE_REGISTER;
  reusableBuffer.moduleSetup.pxx2.registerStep = REGISTER_INIT;

  GuardedPxx2Frame guardedFrame(4);
  ASSERT_TRUE(guardedFrame.isValid());

  uint8_t * frame = guardedFrame.data();
  frame[1] = PXX2_TYPE_C_MODULE;
  frame[2] = PXX2_TYPE_ID_REGISTER;
  frame[3] = 0;
  frame[4] = 0;

  processPXX2Frame(INTERNAL_MODULE, frame, nullptr, nullptr);

  EXPECT_EQ(reusableBuffer.moduleSetup.pxx2.registerStep, REGISTER_INIT);
}

TEST_F(PulsesTest, pxx2RejectsShortBindFrame)
{
  moduleState[INTERNAL_MODULE].bindInformation =
      &reusableBuffer.moduleSetup.bindInformation;
  moduleState[INTERNAL_MODULE].mode = MODULE_MODE_BIND;
  reusableBuffer.moduleSetup.bindInformation.step = BIND_INIT;

  GuardedPxx2Frame guardedFrame(4);
  ASSERT_TRUE(guardedFrame.isValid());

  uint8_t * frame = guardedFrame.data();
  frame[1] = PXX2_TYPE_C_MODULE;
  frame[2] = PXX2_TYPE_ID_BIND;
  frame[3] = 0;
  frame[4] = 0;

  processPXX2Frame(INTERNAL_MODULE, frame, nullptr, nullptr);

  EXPECT_EQ(reusableBuffer.moduleSetup.bindInformation.candidateReceiversCount,
            0);
}

TEST_F(PulsesTest, pxx2RejectsShortHardwareInfoFrame)
{
  ModuleInformation moduleInformation = {};
  moduleState[INTERNAL_MODULE].readModuleInformation(
      &moduleInformation, PXX2_HW_INFO_TX_ID, PXX2_HW_INFO_TX_ID);

  GuardedPxx2Frame guardedFrame(3);
  ASSERT_TRUE(guardedFrame.isValid());

  uint8_t * frame = guardedFrame.data();
  frame[1] = PXX2_TYPE_C_MODULE;
  frame[2] = PXX2_TYPE_ID_HW_INFO;
  frame[3] = PXX2_HW_INFO_TX_ID;

  processPXX2Frame(INTERNAL_MODULE, frame, nullptr, nullptr);

  EXPECT_EQ(moduleInformation.information.modelID, 0);
}

TEST_F(PulsesTest, pxx2RejectsShortModuleSettingsFrame)
{
  ModuleSettings moduleSettings = {};
  moduleState[INTERNAL_MODULE].readModuleSettings(&moduleSettings);

  GuardedPxx2Frame guardedFrame(3);
  ASSERT_TRUE(guardedFrame.isValid());

  uint8_t * frame = guardedFrame.data();
  frame[1] = PXX2_TYPE_C_MODULE;
  frame[2] = PXX2_TYPE_ID_TX_SETTINGS;
  frame[3] = 0;

  processPXX2Frame(INTERNAL_MODULE, frame, nullptr, nullptr);

  EXPECT_EQ(moduleSettings.state, PXX2_SETTINGS_READ);
}

TEST_F(PulsesTest, pxx2RejectsShortReceiverSettingsFrame)
{
  ReceiverSettings receiverSettings = {};
  moduleState[INTERNAL_MODULE].readReceiverSettings(&receiverSettings);

  GuardedPxx2Frame guardedFrame(3);
  ASSERT_TRUE(guardedFrame.isValid());

  uint8_t * frame = guardedFrame.data();
  frame[1] = PXX2_TYPE_C_MODULE;
  frame[2] = PXX2_TYPE_ID_RX_SETTINGS;
  frame[3] = 0;

  processPXX2Frame(INTERNAL_MODULE, frame, nullptr, nullptr);

  EXPECT_EQ(receiverSettings.state, PXX2_SETTINGS_READ);
}

TEST_F(PulsesTest, pxx2RejectsShortTelemetryFrame)
{
  GuardedPxx2Frame guardedFrame(3);
  ASSERT_TRUE(guardedFrame.isValid());

  uint8_t * frame = guardedFrame.data();
  frame[1] = PXX2_TYPE_C_MODULE;
  frame[2] = PXX2_TYPE_ID_TELEMETRY;
  frame[3] = 0;

  processPXX2Frame(INTERNAL_MODULE, frame, nullptr, nullptr);

  SUCCEED();
}
#endif

#if defined(AFHDS3) && defined(HARDWARE_EXTERNAL_MODULE)
TEST_F(PulsesTest, afhds3ApplyConfigRejectsInvalidChannelStart)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_FLYSKY_AFHDS3;
  g_model.moduleData[EXTERNAL_MODULE].channelsStart = MAX_OUTPUT_CHANNELS;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount = 0;
  g_model.moduleData[EXTERNAL_MODULE].failsafeMode = FAILSAFE_CUSTOM;
  g_model.trainerData.mode = 0x34;
  g_model.trainerData.channelsStart = 0x12;

  auto ctx = afhds3::ProtoDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  afhds3::applyModelConfig(EXTERNAL_MODULE);

  auto cfg = afhds3::getConfig(EXTERNAL_MODULE);
  ASSERT_NE(cfg, nullptr);
  EXPECT_EQ(cfg->v0.FailSafe[0], 0);

  afhds3::ProtoDriver.deinit(ctx);
}

TEST_F(PulsesTest, afhds3RejectsShortModuleVersionResponse)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_FLYSKY_AFHDS3;

  auto ctx = afhds3::ProtoDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  feedShortAfhds3Response(ctx, afhds3::COMMAND::MODULE_VERSION, 0);

  afhds3::ProtoDriver.deinit(ctx);
}

TEST_F(PulsesTest, afhds3RejectsTooShortResponseFrame)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_FLYSKY_AFHDS3;

  auto ctx = afhds3::ProtoDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  GuardedAfhds3RxBuffer rxBuffer(3);
  ASSERT_TRUE(rxBuffer.isValid());

  uint8_t len = 0;
  const uint8_t frame[] = {0xC0, 0xFF, 0xC0};
  for (uint8_t byte : frame) {
    afhds3::ProtoDriver.processData(ctx, byte, rxBuffer.data(), &len);
  }

  afhds3::ProtoDriver.deinit(ctx);
  SUCCEED();
}

TEST_F(PulsesTest, afhds3RejectsInvalidRfPower)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_FLYSKY_AFHDS3;
  g_model.moduleData[EXTERNAL_MODULE].afhds3.rfPower = 255;

  auto ctx = afhds3::ProtoDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  afhds3::ProtoDriver.sendPulses(ctx, buffer, nullptr, 0);
  feedShortAfhds3Response(ctx, afhds3::COMMAND::MODULE_READY, 0x02);
  feedShortAfhds3Response(ctx, afhds3::COMMAND::MODULE_STATE, 0x05);
  afhds3::ProtoDriver.sendPulses(ctx, buffer, nullptr, 0);
  feedShortAfhds3Response(ctx, afhds3::COMMAND::MODEL_ID, 0x02);
  afhds3::ProtoDriver.sendPulses(ctx, buffer, nullptr, 0);
  feedShortAfhds3Response(ctx, afhds3::COMMAND::MODULE_MODE, 0x02);
  feedShortAfhds3Response(ctx, afhds3::COMMAND::MODULE_STATE, 0x04);

  auto cfg = afhds3::getConfig(EXTERNAL_MODULE);
  ASSERT_NE(cfg, nullptr);
  cfg->others.dirtyFlag =
      static_cast<uint32_t>(1) << afhds3::DirtyConfig::DC_RX_CMD_TX_PWR;

  afhds3::ProtoDriver.sendPulses(ctx, buffer, nullptr, 0);
  afhds3::ProtoDriver.sendPulses(ctx, buffer, nullptr, 0);
  afhds3::ProtoDriver.sendPulses(ctx, buffer, nullptr, 0);

  uint8_t* moduleBuffer = pulsesGetModuleBuffer(EXTERNAL_MODULE);
  EXPECT_EQ(moduleBuffer[3], afhds3::FRAME_TYPE::REQUEST_SET_EXPECT_DATA);
  EXPECT_EQ(moduleBuffer[4], afhds3::COMMAND::SEND_COMMAND);
  EXPECT_EQ(moduleBuffer[5], 0x13);
  EXPECT_EQ(moduleBuffer[6], 0x20);
  EXPECT_EQ(moduleBuffer[7], 2);
  EXPECT_EQ(moduleBuffer[8], 56);
  EXPECT_EQ(moduleBuffer[9], 0);

  afhds3::ProtoDriver.deinit(ctx);
}
#endif

#if defined(PPM) && defined(HARDWARE_EXTERNAL_MODULE)
TEST_F(PulsesTest, ppmSendPulsesHonorsChannelCount)
{
  modulePortInit();
  g_model.moduleData[EXTERNAL_MODULE].type = MODULE_TYPE_PPM;
  g_model.moduleData[EXTERNAL_MODULE].channelsCount = 0;

  auto ctx = PpmDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t neutralFrame[MODULE_BUFFER_SIZE] = {};
  channelOutputs[0] = 0;
  PpmDriver.sendPulses(ctx, neutralFrame, nullptr, 0);

  uint8_t highFrame[MODULE_BUFFER_SIZE] = {};
  channelOutputs[0] = 1024;
  PpmDriver.sendPulses(ctx, highFrame, nullptr, 0);

  EXPECT_EQ(memcmp(neutralFrame, highFrame, sizeof(neutralFrame)), 0);

  PpmDriver.deinit(ctx);
}
#endif

#if defined(SBUS) && defined(HARDWARE_EXTERNAL_MODULE)
TEST_F(PulsesTest, sbusSendPulsesHonorsChannelCount)
{
  modulePortInit();
  channelOutputs[0] = 1024;

  auto ctx = SBusDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  SBusDriver.sendPulses(ctx, buffer, nullptr, 0);

  uint16_t firstPulse = buffer[1] | ((buffer[2] & 0x07) << 8);
  EXPECT_EQ(firstPulse, 992);

  SBusDriver.deinit(ctx);
}
#endif
