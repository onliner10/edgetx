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

#if defined(PXX1)
#include "pulses/pxx1.h"
#endif

#if defined(PPM)
#include "pulses/ppm.h"
#endif

extern uint8_t getRequiredProtocol(uint8_t module);

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
