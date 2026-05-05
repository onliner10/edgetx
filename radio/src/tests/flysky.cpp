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

#include "telemetry/flysky_ibus.h"

#include <sys/mman.h>
#include <unistd.h>

namespace {

class GuardedFlySkyFrame
{
 public:
  explicit GuardedFlySkyFrame(size_t size):
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

  ~GuardedFlySkyFrame()
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

}  // namespace

TEST(FlySky, shortAaPacketDoesNotReadPastFrame)
{
  GuardedFlySkyFrame frame(28);
  ASSERT_TRUE(frame.isValid());

  processFlySkyPacket(frame.data(), 28);
}

TEST(FlySky, shortAcSensorDoesNotReadPastFrame)
{
  GuardedFlySkyFrame frame(28);
  ASSERT_TRUE(frame.isValid());

  frame.data()[3] = 19;

  processFlySkyPacketAC(frame.data(), 28);
}

TEST(FlySky, shortAfhds3RfModuleSensorDoesNotReadPastFrame)
{
  GuardedFlySkyFrame frame(8);
  ASSERT_TRUE(frame.isValid());

  constexpr uint8_t sensorTypeRfModule = 0x56;
  frame.data()[1] = sensorTypeRfModule;

  processFlySkyAFHDS3Sensor(frame.data(), 5);
}
