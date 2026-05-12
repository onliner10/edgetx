/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "gtests.h"
#include "telemetry/battery_monitor.h"

static void setFlightBatteryVoltageSensor(uint8_t index, int32_t centivolts)
{
  g_model.telemetrySensors[index].init("VFAS", UNIT_VOLTS, 2);
  telemetryItems[index].value = centivolts;
  telemetryItems[index].setFresh();
  g_model.batteryMonitors[0].sourceIndex = index + 1;
}

class BatteryMonitorPolicyTest : public EdgeTxTest {};

TEST_F(BatteryMonitorPolicyTest, CapacityThresholdsUseConsumedCapacity)
{
  EXPECT_FALSE(flightBatteryCapacityThresholdReached(1429, 2200, 65));
  EXPECT_TRUE(flightBatteryCapacityThresholdReached(1430, 2200, 65));
  EXPECT_TRUE(flightBatteryCapacityThresholdReached(1760, 2200, 80));
}

TEST_F(BatteryMonitorPolicyTest, CapacityThresholdRejectsInvalidInputs)
{
  EXPECT_FALSE(flightBatteryCapacityThresholdReached(100, 0, 60));
  EXPECT_FALSE(flightBatteryCapacityThresholdReached(0, 2200, 60));
  EXPECT_FALSE(flightBatteryCapacityThresholdReached(-1, 2200, 60));
}

TEST_F(BatteryMonitorPolicyTest, CapacityThresholdHandlesLargeTelemetryValues)
{
  EXPECT_TRUE(
      flightBatteryCapacityThresholdReached(INT32_MAX, INT16_MAX, 90));
}

TEST_F(BatteryMonitorPolicyTest, VoltageThresholdsAreChemistrySpecific)
{
  EXPECT_EQ(350, flightBatteryVoltageThresholdPerCellCentivolts(BATTERY_TYPE_LIPO));
  EXPECT_EQ(300, flightBatteryVoltageThresholdPerCellCentivolts(BATTERY_TYPE_LIION));
  EXPECT_EQ(280, flightBatteryVoltageThresholdPerCellCentivolts(BATTERY_TYPE_LIFE));
  EXPECT_EQ(105, flightBatteryVoltageThresholdPerCellCentivolts(BATTERY_TYPE_NIMH));
  EXPECT_EQ(180, flightBatteryVoltageThresholdPerCellCentivolts(BATTERY_TYPE_PB));
}

TEST_F(BatteryMonitorPolicyTest, LipoMatchConstants)
{
  EXPECT_EQ(65, FLIGHT_BATTERY_CAPACITY_THRESHOLDS[0]);
  EXPECT_EQ(80, FLIGHT_BATTERY_CAPACITY_THRESHOLDS[3]);
  EXPECT_EQ(330, FLIGHT_BATTERY_LIPO_BACKUP_MIN_PER_CELL_CV);
  EXPECT_EQ(100, FLIGHT_BATTERY_NO_BATTERY_MAX_CV);
  EXPECT_EQ(300, FLIGHT_BATTERY_LIPO_MATCH_MIN_PER_CELL_CV);
  EXPECT_EQ(435, FLIGHT_BATTERY_LIPO_MATCH_MAX_PER_CELL_CV);
  EXPECT_EQ(2, FLIGHT_BATTERY_PRESENT_DEBOUNCE_SECONDS);
  EXPECT_EQ(3, FLIGHT_BATTERY_NO_BATTERY_DEBOUNCE_SECONDS);
}

TEST_F(BatteryMonitorPolicyTest, Lipo3SAt1140CvMatches3SNot4S)
{
  uint16_t voltage3s = 1140;
  EXPECT_TRUE(flightBatteryPackMatchesLipo(voltage3s, 3));
  EXPECT_FALSE(flightBatteryPackMatchesLipo(voltage3s, 4));

  uint8_t candidates1[] = {3};
  EXPECT_EQ(BatteryLipoMatchResult::Exact,
            flightBatteryMatchLipoCandidates(voltage3s, candidates1, 1));

  uint8_t candidates2[] = {3, 4};
  EXPECT_EQ(BatteryLipoMatchResult::Exact,
            flightBatteryMatchLipoCandidates(voltage3s, candidates2, 2));
}

TEST_F(BatteryMonitorPolicyTest, Lipo4SAt1520CvMatches4SNot3S)
{
  uint16_t voltage4s = 1520;
  EXPECT_TRUE(flightBatteryPackMatchesLipo(voltage4s, 4));
  EXPECT_FALSE(flightBatteryPackMatchesLipo(voltage4s, 3));

  uint8_t candidates[] = {3, 4};
  EXPECT_EQ(BatteryLipoMatchResult::Exact,
            flightBatteryMatchLipoCandidates(voltage4s, candidates, 2));
}

TEST_F(BatteryMonitorPolicyTest, LipoBoundaryMinPerCell)
{
  EXPECT_TRUE(flightBatteryPackMatchesLipo(300, 1));
  EXPECT_FALSE(flightBatteryPackMatchesLipo(299, 1));

  EXPECT_TRUE(flightBatteryPackMatchesLipo(900, 3));
  EXPECT_FALSE(flightBatteryPackMatchesLipo(899, 3));
}

TEST_F(BatteryMonitorPolicyTest, LipoBoundaryMaxPerCell)
{
  EXPECT_TRUE(flightBatteryPackMatchesLipo(435, 1));
  EXPECT_FALSE(flightBatteryPackMatchesLipo(436, 1));

  EXPECT_TRUE(flightBatteryPackMatchesLipo(1305, 3));
  EXPECT_FALSE(flightBatteryPackMatchesLipo(1306, 3));
}

TEST_F(BatteryMonitorPolicyTest, NoBatteryBelow100Cv)
{
  EXPECT_FALSE(flightBatteryPackMatchesLipo(0, 3));
  EXPECT_FALSE(flightBatteryPackMatchesLipo(99, 3));
  EXPECT_FALSE(flightBatteryPackMatchesLipo(100, 3));

  uint8_t candidates[] = {3, 4};
  EXPECT_EQ(BatteryLipoMatchResult::None,
            flightBatteryMatchLipoCandidates(99, candidates, 2));
}

TEST_F(BatteryMonitorPolicyTest, InvalidCellCountReturnsFalse)
{
  EXPECT_FALSE(flightBatteryPackMatchesLipo(1200, 0));

  uint8_t candidates[] = {0, 3};
  EXPECT_EQ(BatteryLipoMatchResult::Exact,
            flightBatteryMatchLipoCandidates(1200, candidates, 2));
}

TEST_F(BatteryMonitorPolicyTest, AmbiguousCandidatesReturnsAmbiguous)
{
  uint8_t candidates[] = {2, 3, 4};
  EXPECT_EQ(BatteryLipoMatchResult::Exact,
            flightBatteryMatchLipoCandidates(900, candidates, 3));

  uint8_t candidates2[] = {3, 4};
  EXPECT_EQ(BatteryLipoMatchResult::Ambiguous,
            flightBatteryMatchLipoCandidates(1200, candidates2, 2));
}

TEST_F(BatteryMonitorPolicyTest, EmptyCandidatesReturnsNone)
{
  uint8_t empty[] = {};
  EXPECT_EQ(BatteryLipoMatchResult::None,
            flightBatteryMatchLipoCandidates(1200, empty, 0));
}

class BatteryRuntimeTest : public EdgeTxTest {};

TEST_F(BatteryRuntimeTest, SessionStateEnumValues)
{
  EXPECT_EQ(0, (uint8_t)FlightBatterySessionState::Unknown);
  EXPECT_EQ(1, (uint8_t)FlightBatterySessionState::WaitingForVoltage);
  EXPECT_EQ(2, (uint8_t)FlightBatterySessionState::NoBatteryObserved);
  EXPECT_EQ(3, (uint8_t)FlightBatterySessionState::NeedsConfirmation);
  EXPECT_EQ(4, (uint8_t)FlightBatterySessionState::Confirmed);
  EXPECT_EQ(5, (uint8_t)FlightBatterySessionState::ConfirmedWaitingForVoltage);
  EXPECT_EQ(6, (uint8_t)FlightBatterySessionState::VoltageMismatch);
  EXPECT_EQ(7, (uint8_t)FlightBatterySessionState::NeedsConfiguration);
}

TEST_F(BatteryRuntimeTest, InitialStateIsUnknown)
{
  resetFlightBatteryRuntimeState();
  EXPECT_EQ(FlightBatterySessionState::Unknown, flightBatterySessionState(0));
  EXPECT_EQ(FlightBatterySessionState::Unknown, flightBatterySessionState(1));
}

TEST_F(BatteryRuntimeTest, ArmingAllowedWithNoMonitorsEnabled)
{
  resetFlightBatteryRuntimeState();
  EXPECT_TRUE(flightBatteryArmingAllowed());
}

TEST_F(BatteryRuntimeTest, ArmingDisallowedWhenMonitorNotConfirmed)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].enabled = true;
  invalidateFlightBatteryMonitor(0);
  EXPECT_FALSE(flightBatteryArmingAllowed());
}

TEST_F(BatteryRuntimeTest, ArmingAllowedWithValidManualConfigAndMissingVoltage)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].enabled = true;
  g_model.batteryMonitors[0].batteryType = BATTERY_TYPE_LIPO;
  g_model.batteryMonitors[0].cellCount = 3;
  g_model.batteryMonitors[0].capacity = 2200;
  invalidateFlightBatteryMonitor(0);
  EXPECT_TRUE(flightBatteryArmingAllowed());
}

TEST_F(BatteryRuntimeTest, ArmingAllowedWithFreshZeroVoltageSensor)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].enabled = true;
  g_model.batteryMonitors[0].batteryType = BATTERY_TYPE_LIPO;
  g_model.batteryMonitors[0].cellCount = 3;
  g_model.batteryMonitors[0].capacity = 2200;
  setFlightBatteryVoltageSensor(0, 0);

  for (uint8_t i = 0; i < FLIGHT_BATTERY_NO_BATTERY_DEBOUNCE_SECONDS; i++) {
    updateFlightBatterySessions();
  }

  EXPECT_EQ(FlightBatterySessionState::NoBatteryObserved,
            flightBatterySessionState(0));
  EXPECT_TRUE(flightBatteryArmingAllowed());
}

TEST_F(BatteryRuntimeTest, ArmingDisallowedOnVoltageMismatch)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].enabled = true;
  g_model.batteryMonitors[0].batteryType = BATTERY_TYPE_LIPO;
  g_model.batteryMonitors[0].cellCount = 3;
  g_model.batteryMonitors[0].capacity = 2200;
  setFlightBatteryVoltageSensor(0, 1600);

  updateFlightBatterySessions();
  updateFlightBatterySessions();

  EXPECT_EQ(FlightBatterySessionState::VoltageMismatch,
            flightBatterySessionState(0));
  EXPECT_FALSE(flightBatteryArmingAllowed());
  EXPECT_EQ(ArmingBlockReason::BatteryVoltageMismatch,
            flightBatteryArmingBlockReason());
}

TEST_F(BatteryRuntimeTest, ArmingDisallowedWhenAllMonitorsDisabled)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].enabled = false;
  EXPECT_TRUE(flightBatteryArmingAllowed());
}

TEST_F(BatteryRuntimeTest, NeedsPromptReturnsFalseWhenNoMonitorNeedsIt)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].enabled = false;
  uint8_t monitor = 99;
  EXPECT_FALSE(flightBatteryNeedsPrompt(&monitor));
}

TEST_F(BatteryRuntimeTest, PromptPackMaskReturnsZeroWhenNoCompatiblePacks)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].compatiblePackMask = 0;
  EXPECT_EQ(0, flightBatteryPromptPackMask(0));
}

TEST_F(BatteryRuntimeTest, PromptPackMaskReturnsConfiguredMask)
{
  resetFlightBatteryRuntimeState();
  g_eeGeneral.batteryPacks[0].active = true;
  g_eeGeneral.batteryPacks[0].cellCount = 3;
  g_eeGeneral.batteryPacks[1].active = true;
  g_eeGeneral.batteryPacks[1].cellCount = 4;
  g_model.batteryMonitors[0].enabled = true;
  g_model.batteryMonitors[0].compatiblePackMask = 0x03;
  setFlightBatteryVoltageSensor(0, 1200);
  updateFlightBatterySessions();
  updateFlightBatterySessions();
  EXPECT_EQ(0x03, flightBatteryPromptPackMask(0));
}

TEST_F(BatteryRuntimeTest, BlockedArmingRequestsPromptAgain)
{
  resetFlightBatteryRuntimeState();
  g_eeGeneral.batteryPacks[0].active = true;
  g_eeGeneral.batteryPacks[0].cellCount = 3;
  g_eeGeneral.batteryPacks[1].active = true;
  g_eeGeneral.batteryPacks[1].cellCount = 4;
  g_model.batteryMonitors[0].enabled = true;
  g_model.batteryMonitors[0].compatiblePackMask = 0x03;
  setFlightBatteryVoltageSensor(0, 1200);
  updateFlightBatterySessions();
  updateFlightBatterySessions();

  uint8_t monitor = 99;
  EXPECT_TRUE(flightBatteryNeedsPrompt(&monitor));
  EXPECT_EQ(0, monitor);
  markFlightBatteryPromptShown(0);
  EXPECT_FALSE(flightBatteryNeedsPrompt(nullptr));

  requestFlightBatteryBlockedPrompt();
  EXPECT_TRUE(flightBatteryNeedsPrompt(&monitor));
  EXPECT_EQ(0, monitor);
}

TEST_F(BatteryRuntimeTest, PromptAllowsManualWhenCellCountIsValidLipo)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].batteryType = BATTERY_TYPE_LIPO;
  g_model.batteryMonitors[0].cellCount = 3;
  g_model.batteryMonitors[0].capacity = 2200;
  EXPECT_TRUE(flightBatteryPromptAllowsManual(0));
}

TEST_F(BatteryRuntimeTest, PromptAllowsManualReturnsFalseForInvalidCellCount)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].cellCount = 0;
  EXPECT_FALSE(flightBatteryPromptAllowsManual(0));
}

TEST_F(BatteryRuntimeTest, ConfirmFlightBatteryPackSetsConfirmedState)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].enabled = true;
  g_model.batteryMonitors[0].batteryType = BATTERY_TYPE_LIPO;
  g_model.batteryMonitors[0].cellCount = 3;
  g_model.batteryMonitors[0].capacity = 2200;
  flightBatteryRuntimeState[0].state = FlightBatterySessionState::NeedsConfirmation;
  setFlightBatteryVoltageSensor(0, 1140);
  EXPECT_TRUE(confirmFlightBatteryPack(0, 0));
  EXPECT_EQ(FlightBatterySessionState::Confirmed, flightBatterySessionState(0));
}

TEST_F(BatteryRuntimeTest, ConfirmFlightBatteryPackFromGlobalSlot)
{
  resetFlightBatteryRuntimeState();
  g_eeGeneral.batteryPacks[0].active = true;
  g_eeGeneral.batteryPacks[0].cellCount = 3;
  g_eeGeneral.batteryPacks[0].capacity = 2200;
  g_model.batteryMonitors[0].enabled = true;
  flightBatteryRuntimeState[0].state = FlightBatterySessionState::NeedsConfirmation;
  flightBatteryRuntimeState[0].promptPackMask = 0x01;
  setFlightBatteryVoltageSensor(0, 1140);
  EXPECT_TRUE(confirmFlightBatteryPack(0, 1));
  EXPECT_EQ(FlightBatterySessionState::Confirmed, flightBatterySessionState(0));
  EXPECT_EQ(1, g_model.batteryMonitors[0].selectedPackSlot);
  EXPECT_EQ(BATTERY_TYPE_LIPO, g_model.batteryMonitors[0].batteryType);
}

TEST_F(BatteryRuntimeTest, ArmedStatePreventsDemotionToUnknown)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].enabled = true;
  g_model.batteryMonitors[0].batteryType = BATTERY_TYPE_LIPO;
  g_model.batteryMonitors[0].cellCount = 3;
  g_model.batteryMonitors[0].capacity = 2200;
  flightBatteryRuntimeState[0].state = FlightBatterySessionState::NeedsConfirmation;
  setFlightBatteryVoltageSensor(0, 1140);
  EXPECT_TRUE(confirmFlightBatteryPack(0, 0));
  EXPECT_EQ(FlightBatterySessionState::Confirmed, flightBatterySessionState(0));
}

TEST_F(BatteryRuntimeTest, ConfirmationCopiesGlobalPackIntoMonitor)
{
  resetFlightBatteryRuntimeState();
  g_eeGeneral.batteryPacks[0].active = 1;
  strncpy(g_eeGeneral.batteryPacks[0].name, "3S 2200", 8);
  g_eeGeneral.batteryPacks[0].cellCount = 3;
  g_eeGeneral.batteryPacks[0].capacity = 2200;

  g_model.batteryMonitors[0].enabled = true;
  flightBatteryRuntimeState[0].state = FlightBatterySessionState::NeedsConfirmation;
  flightBatteryRuntimeState[0].promptPackMask = 0x01;
  setFlightBatteryVoltageSensor(0, 1140);
  EXPECT_TRUE(confirmFlightBatteryPack(0, 1));

  EXPECT_EQ(BATTERY_TYPE_LIPO, g_model.batteryMonitors[0].batteryType);
  EXPECT_EQ(3, g_model.batteryMonitors[0].cellCount);
  EXPECT_EQ(2200, g_model.batteryMonitors[0].capacity);
  EXPECT_EQ(1, g_model.batteryMonitors[0].selectedPackSlot);
  EXPECT_EQ(FlightBatterySessionState::Confirmed, flightBatterySessionState(0));
}

TEST_F(BatteryRuntimeTest, ConfirmationRejectsInvalidManual)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].enabled = true;
  g_model.batteryMonitors[0].batteryType = BATTERY_TYPE_LIION;
  g_model.batteryMonitors[0].cellCount = 3;
  g_model.batteryMonitors[0].capacity = 2200;

  confirmFlightBatteryPack(0, 0);

  EXPECT_NE(FlightBatterySessionState::Confirmed, flightBatterySessionState(0));
}

TEST_F(BatteryRuntimeTest, ConfirmationRejectsZeroCapacityManual)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].enabled = true;
  g_model.batteryMonitors[0].batteryType = BATTERY_TYPE_LIPO;
  g_model.batteryMonitors[0].cellCount = 3;
  g_model.batteryMonitors[0].capacity = 0;

  confirmFlightBatteryPack(0, 0);

  EXPECT_NE(FlightBatterySessionState::Confirmed, flightBatterySessionState(0));
}

TEST_F(BatteryRuntimeTest, ConfirmationRejectsZeroCellCountManual)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].enabled = true;
  g_model.batteryMonitors[0].batteryType = BATTERY_TYPE_LIPO;
  g_model.batteryMonitors[0].cellCount = 0;
  g_model.batteryMonitors[0].capacity = 2200;

  confirmFlightBatteryPack(0, 0);

  EXPECT_NE(FlightBatterySessionState::Confirmed, flightBatterySessionState(0));
}

TEST_F(BatteryRuntimeTest, ConfirmationAcceptsValidManual)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].enabled = true;
  g_model.batteryMonitors[0].batteryType = BATTERY_TYPE_LIPO;
  g_model.batteryMonitors[0].cellCount = 3;
  g_model.batteryMonitors[0].capacity = 2200;
  flightBatteryRuntimeState[0].state = FlightBatterySessionState::NeedsConfirmation;
  setFlightBatteryVoltageSensor(0, 1140);

  EXPECT_TRUE(confirmFlightBatteryPack(0, 0));

  EXPECT_EQ(FlightBatterySessionState::Confirmed, flightBatterySessionState(0));
}

TEST_F(BatteryRuntimeTest, CapacityAlertOnlyFiresAfterConfirmation)
{
  resetFlightBatteryRuntimeState();
  g_model.batteryMonitors[0].capacity = 2200;
  g_model.batteryMonitors[0].capAlertEnabled = 1;

  EXPECT_FALSE(checkFlightBatteryCapacityAlert(0, g_model.batteryMonitors[0], 1429));

  flightBatteryRuntimeState[0].state = FlightBatterySessionState::Confirmed;

  EXPECT_TRUE(checkFlightBatteryCapacityAlert(0, g_model.batteryMonitors[0], 1430));
}

TEST_F(BatteryRuntimeTest, CapacityAlertUsesSessionDelta)
{
  resetFlightBatteryRuntimeState();
  flightBatteryRuntimeState[0].state = FlightBatterySessionState::Confirmed;
  flightBatteryRuntimeState[0].consumedBaselineMah = 500;
  g_model.batteryMonitors[0].capacity = 2200;
  g_model.batteryMonitors[0].capAlertEnabled = 1;

  EXPECT_FALSE(checkFlightBatteryCapacityAlert(0, g_model.batteryMonitors[0], 1929));

  EXPECT_TRUE(checkFlightBatteryCapacityAlert(0, g_model.batteryMonitors[0], 1930));
}

TEST_F(BatteryRuntimeTest, CapacityAlertWithSensorResetUsesCurrentAsBaseline)
{
  resetFlightBatteryRuntimeState();
  flightBatteryRuntimeState[0].state = FlightBatterySessionState::Confirmed;
  flightBatteryRuntimeState[0].consumedBaselineMah = 500;
  g_model.batteryMonitors[0].capacity = 2200;
  g_model.batteryMonitors[0].capAlertEnabled = 1;

  EXPECT_FALSE(checkFlightBatteryCapacityAlert(0, g_model.batteryMonitors[0], 300));

  EXPECT_TRUE(checkFlightBatteryCapacityAlert(0, g_model.batteryMonitors[0], 1930));
}
