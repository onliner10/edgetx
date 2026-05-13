#include "gtests.h"
#include "battery_math.h"

class BatteryMathTest : public EdgeTxTest {};

TEST_F(BatteryMathTest, FullCharge)
{
  EXPECT_EQ(100, txBatteryPercent(84));
}

TEST_F(BatteryMathTest, NominalVoltage)
{
  EXPECT_EQ(25, txBatteryPercent(74));
}

TEST_F(BatteryMathTest, LowVoltage)
{
  EXPECT_EQ(0, txBatteryPercent(66));
}

TEST_F(BatteryMathTest, BelowTableClamps)
{
  EXPECT_EQ(0, txBatteryPercent(65));
  EXPECT_EQ(0, txBatteryPercent(0));
}

TEST_F(BatteryMathTest, AboveTableClamps)
{
  EXPECT_EQ(100, txBatteryPercent(85));
  EXPECT_EQ(100, txBatteryPercent(90));
}

TEST_F(BatteryMathTest, OcvExactPoints)
{
  EXPECT_EQ(0, txBatteryPercent(66));
  EXPECT_EQ(6, txBatteryPercent(70));
  EXPECT_EQ(12, txBatteryPercent(72));
  EXPECT_EQ(25, txBatteryPercent(74));
  EXPECT_EQ(45, txBatteryPercent(76));
  EXPECT_EQ(65, txBatteryPercent(78));
  EXPECT_EQ(80, txBatteryPercent(80));
  EXPECT_EQ(90, txBatteryPercent(82));
  EXPECT_EQ(100, txBatteryPercent(84));
}

TEST_F(BatteryMathTest, LinearInterpolation375)
{
  EXPECT_EQ(35, txBatteryPercent(75));
}

TEST_F(BatteryMathTest, LinearInterpolation365)
{
  EXPECT_EQ(18, txBatteryPercent(73));
}

TEST_F(BatteryMathTest, PerCellExact)
{
  EXPECT_EQ(0, batteryPctFromPerCellCv(330));
  EXPECT_EQ(6, batteryPctFromPerCellCv(350));
  EXPECT_EQ(12, batteryPctFromPerCellCv(360));
  EXPECT_EQ(25, batteryPctFromPerCellCv(370));
  EXPECT_EQ(45, batteryPctFromPerCellCv(380));
  EXPECT_EQ(65, batteryPctFromPerCellCv(390));
  EXPECT_EQ(80, batteryPctFromPerCellCv(400));
  EXPECT_EQ(90, batteryPctFromPerCellCv(410));
  EXPECT_EQ(100, batteryPctFromPerCellCv(420));
}

TEST_F(BatteryMathTest, PerCellInterpolation)
{
  EXPECT_EQ(18, batteryPctFromPerCellCv(365));
  EXPECT_EQ(35, batteryPctFromPerCellCv(375));
}

TEST_F(BatteryMathTest, PerCellClampLow)
{
  EXPECT_EQ(0, batteryPctFromPerCellCv(329));
  EXPECT_EQ(0, batteryPctFromPerCellCv(0));
}

TEST_F(BatteryMathTest, PerCellClampHigh)
{
  EXPECT_EQ(100, batteryPctFromPerCellCv(421));
  EXPECT_EQ(100, batteryPctFromPerCellCv(500));
}
