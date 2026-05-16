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

#include "gtest/gtest.h"
#include "gtests.h"
#include "telemetry/spektrum.h"
#include "telemetry/telemetry.h"

namespace {

constexpr uint8_t SPEKTRUM_I2C_RPM = 0x7E;
constexpr uint8_t SPEKTRUM_I2C_QOS = 0x7F;

void putSpektrumU16(uint8_t *packet, uint8_t startByte, uint16_t value)
{
  packet[4 + startByte] = static_cast<uint8_t>(value >> 8);
  packet[4 + startByte + 1] = static_cast<uint8_t>(value);
}

void resetSpektrumTestTelemetry()
{
  TELEMETRY_RESET();
  telemetryData.rssi.reset();
  telemetryClearRfAlarm();
  telemetryStreaming = 0;
  telemetryState = TELEMETRY_INIT;
}

}  // namespace

TEST(Spektrum, StandardQosDoesNotUseTelemetryPacketRssiForRfAlarms)
{
  resetSpektrumTestTelemetry();

  uint8_t packet[SPEKTRUM_TELEMETRY_LENGTH] = {};
  packet[1] = 1;
  packet[2] = SPEKTRUM_I2C_QOS;
  processSpektrumPacket(packet);

  EXPECT_TRUE(TELEMETRY_STREAMING());
  EXPECT_FALSE(TELEMETRY_RSSI_AVAILABLE());
  EXPECT_EQ(0, TELEMETRY_RSSI());
  EXPECT_EQ(TELEMETRY_RF_ALARM_NONE, telemetryRfAlarmLevel());
}

TEST(Spektrum, LemonRxQosKeepsRssiAlarmSource)
{
  resetSpektrumTestTelemetry();

  uint8_t packet[SPEKTRUM_TELEMETRY_LENGTH] = {};
  packet[2] = SPEKTRUM_I2C_QOS;
  putSpektrumU16(packet, 0, 73);
  putSpektrumU16(packet, 2, 0x8000);
  putSpektrumU16(packet, 4, 0x8000);
  putSpektrumU16(packet, 6, 0x8000);
  putSpektrumU16(packet, 8, 0x8000);
  putSpektrumU16(packet, 10, 0x7FFF);
  putSpektrumU16(packet, 12, 0xFFFF);
  processSpektrumPacket(packet);

  EXPECT_TRUE(TELEMETRY_RSSI_AVAILABLE());
  EXPECT_EQ(73, TELEMETRY_RSSI());
  EXPECT_EQ(TELEMETRY_RF_ALARM_NONE, telemetryRfAlarmLevel());
}

TEST(Spektrum, RpmReceiverPercentProvidesRssiAlarmSource)
{
  resetSpektrumTestTelemetry();

  uint8_t packet[SPEKTRUM_TELEMETRY_LENGTH] = {};
  packet[2] = SPEKTRUM_I2C_RPM;
  packet[4 + 6] = 67;
  packet[4 + 7] = 0;
  processSpektrumPacket(packet);

  EXPECT_TRUE(TELEMETRY_RSSI_AVAILABLE());
  EXPECT_EQ(67, TELEMETRY_RSSI());
}

TEST(Spektrum, FlightLogFrameLossBurstRaisesWarning)
{
  resetSpektrumTestTelemetry();

  uint8_t packet[SPEKTRUM_TELEMETRY_LENGTH] = {};
  packet[2] = SPEKTRUM_I2C_QOS;
  processSpektrumPacket(packet);
  telemetryState = TELEMETRY_OK;

  putSpektrumU16(packet, 8, 44);
  processSpektrumPacket(packet);
  EXPECT_EQ(TELEMETRY_RF_ALARM_NONE, telemetryRfAlarmLevel());

  putSpektrumU16(packet, 8, 45);
  processSpektrumPacket(packet);
  EXPECT_EQ(TELEMETRY_RF_ALARM_WARNING, telemetryRfAlarmLevel());
}

TEST(Spektrum, FlightLogHoldIncreaseRaisesCritical)
{
  resetSpektrumTestTelemetry();

  uint8_t packet[SPEKTRUM_TELEMETRY_LENGTH] = {};
  packet[2] = SPEKTRUM_I2C_QOS;
  processSpektrumPacket(packet);
  telemetryState = TELEMETRY_OK;

  putSpektrumU16(packet, 10, 1);
  processSpektrumPacket(packet);

  EXPECT_EQ(TELEMETRY_RF_ALARM_CRITICAL, telemetryRfAlarmLevel());
  EXPECT_FALSE(TELEMETRY_RSSI_AVAILABLE());
}
