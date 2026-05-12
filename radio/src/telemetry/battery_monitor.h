/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once

#include <inttypes.h>

#include "datastructs.h"

constexpr uint8_t FLIGHT_BATTERY_VOLTAGE_DEBOUNCE_SECONDS = 10;
constexpr uint8_t FLIGHT_BATTERY_CAPACITY_THRESHOLDS[] = {65, 70, 75, 80};
constexpr uint8_t FLIGHT_BATTERY_CAPACITY_THRESHOLDS_SIZE =
    sizeof(FLIGHT_BATTERY_CAPACITY_THRESHOLDS) / sizeof(FLIGHT_BATTERY_CAPACITY_THRESHOLDS[0]);
constexpr uint16_t FLIGHT_BATTERY_LIPO_BACKUP_MIN_PER_CELL_CV = 330;

constexpr uint16_t FLIGHT_BATTERY_NO_BATTERY_MAX_CV = 100;
constexpr uint16_t FLIGHT_BATTERY_LIPO_MATCH_MIN_PER_CELL_CV = 300;
constexpr uint16_t FLIGHT_BATTERY_LIPO_MATCH_MAX_PER_CELL_CV = 435;
constexpr uint8_t FLIGHT_BATTERY_PRESENT_DEBOUNCE_SECONDS = 2;
constexpr uint8_t FLIGHT_BATTERY_NO_BATTERY_DEBOUNCE_SECONDS = 3;

enum class FlightBatterySessionState : uint8_t {
  Unknown,
  WaitingForVoltage,
  NoBatteryObserved,
  NeedsConfirmation,
  Confirmed,
  ConfirmedWaitingForVoltage,
  VoltageMismatch,
  NeedsConfiguration,
};

struct FlightBatteryRuntimeState {
  FlightBatterySessionState state = FlightBatterySessionState::Unknown;
  uint8_t presentSeconds = 0;
  uint8_t absentSeconds = 0;
  uint8_t confirmedPackSlot = 0;
  uint16_t promptPackMask = 0;
  int32_t consumedBaselineMah = 0;
  uint8_t capacityMask = 0;
  uint8_t voltageLowSeconds = 0;
  bool voltageAlerted = false;
  bool promptShown = false;
};

extern FlightBatteryRuntimeState flightBatteryRuntimeState[MAX_BATTERY_MONITORS];

void resetFlightBatteryRuntimeState();
void updateFlightBatterySessions();
bool flightBatteryArmingAllowed();

enum class ArmingBlockReason : uint8_t {
  None,
  BatteryUnknown,
  BatteryVoltageUnavailable,
  BatteryVoltageMismatch,
  BatteryNeedsConfiguration,
};

ArmingBlockReason flightBatteryArmingBlockReason();
ArmingBlockReason consumeArmingBlockReason();
void setArmingBlockReason(ArmingBlockReason reason);
FlightBatterySessionState flightBatterySessionState(uint8_t monitor);
bool flightBatteryNeedsPrompt(uint8_t* monitor);
uint16_t flightBatteryPromptPackMask(uint8_t monitor);
bool flightBatteryPromptAllowsManual(uint8_t monitor);
void requestFlightBatteryPrompt(uint8_t monitor);
void requestFlightBatteryBlockedPrompt();
void markFlightBatteryPromptShown(uint8_t monitor);
bool confirmFlightBatteryPack(uint8_t monitor, uint8_t selectedPackSlot);
void invalidateFlightBatteryMonitor(uint8_t monitor);
void invalidateFlightBatteryPackSlot(uint8_t slot);
bool checkFlightBatteryCapacityAlert(uint8_t monitorIndex,
                                     const BatteryMonitorData& config,
                                     int32_t consumed);

enum class BatteryLipoMatchResult { None, Exact, Ambiguous };

constexpr bool flightBatteryPackMatchesLipo(uint16_t packVoltageCv, uint8_t cellCount)
{
  if (cellCount == 0 || packVoltageCv < FLIGHT_BATTERY_NO_BATTERY_MAX_CV) {
    return false;
  }

  return packVoltageCv >=
             uint16_t(FLIGHT_BATTERY_LIPO_MATCH_MIN_PER_CELL_CV * cellCount) &&
         packVoltageCv <=
             uint16_t(FLIGHT_BATTERY_LIPO_MATCH_MAX_PER_CELL_CV * cellCount);
}

constexpr BatteryLipoMatchResult flightBatteryMatchLipoCandidates(
    uint16_t packVoltageCv, const uint8_t* candidates, uint8_t candidateCount)
{
  if (candidateCount == 0 || packVoltageCv < FLIGHT_BATTERY_NO_BATTERY_MAX_CV) {
    return BatteryLipoMatchResult::None;
  }

  bool hasMatch = false;
  uint8_t matchCount = 0;

  for (uint8_t i = 0; i < candidateCount; ++i) {
    if (flightBatteryPackMatchesLipo(packVoltageCv, candidates[i])) {
      hasMatch = true;
      ++matchCount;
      if (matchCount > 1) {
        return BatteryLipoMatchResult::Ambiguous;
      }
    }
  }

  return hasMatch ? BatteryLipoMatchResult::Exact : BatteryLipoMatchResult::None;
}

inline uint16_t flightBatteryVoltageThresholdPerCellCentivolts(BatteryType type)
{
  switch (type) {
    case BATTERY_TYPE_LIION:
      return 300;
    case BATTERY_TYPE_LIFE:
      return 280;
    case BATTERY_TYPE_NIMH:
      return 105;
    case BATTERY_TYPE_PB:
      return 180;
    case BATTERY_TYPE_LIPO:
    default:
      return 350;
  }
}

inline bool flightBatteryCapacityThresholdReached(int32_t consumed,
                                                  int16_t capacity,
                                                  uint8_t thresholdPercent)
{
  if (capacity <= 0 || consumed <= 0) return false;

  return int64_t(consumed) * 100 >= int64_t(capacity) * thresholdPercent;
}
