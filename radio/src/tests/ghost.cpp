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

#include "gtests.h"

#if defined(GHOST) && defined(LUA) && defined(HARDWARE_EXTERNAL_MODULE)

#include "hal/module_port.h"
#include "pulses/ghost.h"
#include "pulses/pulses.h"
#include "telemetry/ghost.h"
#include "telemetry/telemetry.h"
#include <sys/mman.h>
#include <unistd.h>

class GuardedGhostFrame
{
 public:
  explicit GuardedGhostFrame(size_t size):
      pageSize(sysconf(_SC_PAGESIZE)),
      mappingSize(pageSize * 2),
      frameSize(size)
  {
    mapping = static_cast<uint8_t*>(
        mmap(nullptr, mappingSize, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (mapping == MAP_FAILED) {
      mapping = nullptr;
      return;
    }
    if (mprotect(mapping + pageSize, pageSize, PROT_NONE) != 0) {
      munmap(mapping, mappingSize);
      mapping = nullptr;
      return;
    }
    frame = mapping + pageSize - frameSize;
    memset(frame, 0, frameSize);
  }

  ~GuardedGhostFrame()
  {
    if (mapping != nullptr) {
      munmap(mapping, mappingSize);
    }
  }

  bool isValid() const { return frame != nullptr; }
  uint8_t* data() { return frame; }
  uint8_t& operator[](size_t index) { return frame[index]; }

 private:
  long pageSize = 0;
  size_t mappingSize = 0;
  size_t frameSize = 0;
  uint8_t* mapping = nullptr;
  uint8_t* frame = nullptr;
};

TEST(Ghost, telemetryOutputIsBoundedByModuleBuffer)
{
  modulePortInit();

  auto ctx = GhostDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t telemetryData[TELEMETRY_OUTPUT_BUFFER_SIZE];
  for (uint8_t i = 0; i < TELEMETRY_OUTPUT_BUFFER_SIZE; i += 1) {
    telemetryData[i] = i;
  }
  memcpy(outputTelemetryBuffer.data, telemetryData, sizeof(telemetryData));
  outputTelemetryBuffer.size = sizeof(telemetryData);
  outputTelemetryBuffer.setDestination(TELEMETRY_ENDPOINT_SPORT);

  uint8_t buffer[MODULE_BUFFER_SIZE + 8];
  memset(buffer, 0xA5, sizeof(buffer));

  int16_t channels[MAX_TRAINER_CHANNELS] = {};
  GhostDriver.sendPulses(ctx, buffer, channels, GHOST_CHANNELS_COUNT);

  constexpr uint8_t payloadSize = 12;
  constexpr uint8_t frameSize = payloadSize + 2;
  const uint8_t maxChunks = min<uint8_t>(
      TELEMETRY_OUTPUT_BUFFER_SIZE / payloadSize, MODULE_BUFFER_SIZE / frameSize);
  const uint8_t expectedSize = maxChunks * frameSize;

  for (uint8_t chunk = 0; chunk < maxChunks; chunk += 1) {
    const uint8_t offset = chunk * frameSize;
    EXPECT_EQ(buffer[offset], GHST_ADDR_MODULE_SYM);
    EXPECT_EQ(buffer[offset + 1], payloadSize);
    EXPECT_EQ(memcmp(buffer + offset + 2, telemetryData + chunk * payloadSize,
                     payloadSize),
              0);
  }

  for (uint8_t i = expectedSize; i < sizeof(buffer); i += 1) {
    EXPECT_EQ(buffer[i], 0xA5);
  }
  EXPECT_EQ(outputTelemetryBuffer.destination, TELEMETRY_ENDPOINT_NONE);

  GhostDriver.deinit(ctx);
}

TEST(Ghost, telemetryOutputIsBoundedByTelemetryBuffer)
{
  modulePortInit();

  auto ctx = GhostDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t telemetryData[TELEMETRY_OUTPUT_BUFFER_SIZE];
  for (uint8_t i = 0; i < TELEMETRY_OUTPUT_BUFFER_SIZE; i += 1) {
    telemetryData[i] = i;
  }
  memcpy(outputTelemetryBuffer.data, telemetryData, sizeof(telemetryData));
  outputTelemetryBuffer.size = 255;
  outputTelemetryBuffer.setDestination(TELEMETRY_ENDPOINT_SPORT);

  uint8_t buffer[MODULE_BUFFER_SIZE + 8];
  memset(buffer, 0xA5, sizeof(buffer));

  int16_t channels[MAX_TRAINER_CHANNELS] = {};
  GhostDriver.sendPulses(ctx, buffer, channels, GHOST_CHANNELS_COUNT);

  constexpr uint8_t payloadSize = 12;
  constexpr uint8_t frameSize = payloadSize + 2;
  const uint8_t maxChunks = min<uint8_t>(
      TELEMETRY_OUTPUT_BUFFER_SIZE / payloadSize, MODULE_BUFFER_SIZE / frameSize);
  const uint8_t expectedSize = maxChunks * frameSize;

  for (uint8_t chunk = 0; chunk < maxChunks; chunk += 1) {
    const uint8_t offset = chunk * frameSize;
    EXPECT_EQ(buffer[offset], GHST_ADDR_MODULE_SYM);
    EXPECT_EQ(buffer[offset + 1], payloadSize);
    EXPECT_EQ(memcmp(buffer + offset + 2, telemetryData + chunk * payloadSize,
                     payloadSize),
              0);
  }

  for (uint8_t i = expectedSize; i < sizeof(buffer); i += 1) {
    EXPECT_EQ(buffer[i], 0xA5);
  }
  EXPECT_EQ(outputTelemetryBuffer.destination, TELEMETRY_ENDPOINT_NONE);

  GhostDriver.deinit(ctx);
}

TEST(Ghost, channelsFrameHonorsChannelCount)
{
  modulePortInit();

  auto ctx = GhostDriver.init(EXTERNAL_MODULE);
  ASSERT_NE(ctx, nullptr);

  uint8_t buffer[MODULE_BUFFER_SIZE] = {};
  GhostDriver.sendPulses(ctx, buffer, nullptr, 0);

  GhostDriver.deinit(ctx);
}

TEST(Ghost, shortSyncFrameDoesNotUpdateModuleSync)
{
  ModuleSyncStatus &status = getModuleSyncStatus(EXTERNAL_MODULE);
  status.refreshRate = 4000;
  status.inputLag = 25;
  status.currentLag = 25;

  uint8_t frame[] = {GHST_ADDR_RADIO, 2, GHST_DL_OPENTX_SYNC, 0,
                     0x00,            0x00, 0x27,              0x10,
                     0x00,            0x00, 0x00,              0x64};
  frame[3] = crc8(&frame[2], 1);

  processGhostTelemetryFrame(EXTERNAL_MODULE, frame, sizeof(frame));

  EXPECT_EQ(status.refreshRate, 4000);
  EXPECT_EQ(status.inputLag, 25);
  EXPECT_EQ(status.currentLag, 25);
}

TEST(Ghost, shortLinkStatFrameDoesNotReadPastFrame)
{
  GuardedGhostFrame frame(4);
  ASSERT_TRUE(frame.isValid());
  frame[0] = GHST_ADDR_RADIO;
  frame[1] = 2;
  frame[2] = GHST_DL_LINK_STAT;
  frame[3] = crc8(&frame[2], 1);

  processGhostTelemetryFrame(EXTERNAL_MODULE, frame.data(), 4);

  SUCCEED();
}

TEST(Ghost, shortVtxStatFrameDoesNotReadPastFrame)
{
  GuardedGhostFrame frame(4);
  ASSERT_TRUE(frame.isValid());
  frame[0] = GHST_ADDR_RADIO;
  frame[1] = 2;
  frame[2] = GHST_DL_VTX_STAT;
  frame[3] = crc8(&frame[2], 1);

  processGhostTelemetryFrame(EXTERNAL_MODULE, frame.data(), 4);

  SUCCEED();
}

TEST(Ghost, shortPackStatFrameDoesNotReadPastFrame)
{
  GuardedGhostFrame frame(4);
  ASSERT_TRUE(frame.isValid());
  frame[0] = GHST_ADDR_RADIO;
  frame[1] = 2;
  frame[2] = GHST_DL_PACK_STAT;
  frame[3] = crc8(&frame[2], 1);

  processGhostTelemetryFrame(EXTERNAL_MODULE, frame.data(), 4);

  SUCCEED();
}

#endif
