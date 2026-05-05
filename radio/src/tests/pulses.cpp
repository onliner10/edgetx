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

#if defined(MULTIMODULE)
#include "pulses/multi.h"
#endif

#if defined(SBUS)
#include "pulses/sbus.h"
#endif

#if defined(DSM2)
#include "pulses/dsm2.h"
#endif

#if defined(DSMP)
#include "pulses/dsmp.h"
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
