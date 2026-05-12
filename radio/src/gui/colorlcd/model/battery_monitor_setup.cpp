/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "battery_monitor_setup.h"

#include "button_matrix.h"
#include "choice.h"
#include "edgetx.h"
#include "getset_helpers.h"
#include "numberedit.h"
#include "sourcechoice.h"
#include "toggleswitch.h"
#include "telemetry/battery_monitor.h"

#define SET_DIRTY() storageDirty(EE_MODEL)

static const char* const batteryTypeNames[] = {"LiPo", "Li-ion", "LiFe", "NiMH", "Pb"};

static bool isBatteryMonitorVoltageSensor(uint8_t index)
{
  if (index >= MAX_TELEMETRY_SENSORS) return false;
  const auto& sensor = g_model.telemetrySensors[index];
  return sensor.isAvailable() &&
         (sensor.unit == UNIT_VOLTS || sensor.unit == UNIT_CELLS);
}

static bool isBatteryMonitorCapacitySensor(uint8_t index)
{
  if (index >= MAX_TELEMETRY_SENSORS) return false;
  const auto& sensor = g_model.telemetrySensors[index];
  return sensor.isAvailable() && sensor.unit == UNIT_MAH;
}

static bool isBatteryMonitorSensorSource(int16_t value, uint8_t unit)
{
  if (value == MIXSRC_NONE) return true;
  if (value < MIXSRC_FIRST_TELEM) return false;

  const auto qr = div(value - MIXSRC_FIRST_TELEM, 3);
  if (qr.rem != 0 || qr.quot < 0 || qr.quot >= MAX_TELEMETRY_SENSORS)
    return false;

  return unit == UNIT_VOLTS ? isBatteryMonitorVoltageSensor(qr.quot)
                            : isBatteryMonitorCapacitySensor(qr.quot);
}

static void getFlightPackStatusString(FlightBatterySessionState state,
                                     uint8_t monitor, char* buf,
                                     size_t bufsize)
{
  BatteryMonitorData& config = g_model.batteryMonitors[monitor];

  if (!config.enabled) {
    strncpy(buf, "Monitor disabled", bufsize);
    return;
  }

  if (config.batteryType != BATTERY_TYPE_LIPO) {
    strncpy(buf, "Needs setup", bufsize);
    return;
  }

  switch (state) {
    case FlightBatterySessionState::Unknown:
      strncpy(buf, "Not detected", bufsize);
      break;
    case FlightBatterySessionState::WaitingForVoltage:
      strncpy(buf, "Waiting for voltage", bufsize);
      break;
    case FlightBatterySessionState::NoBatteryObserved:
      strncpy(buf, "No battery detected", bufsize);
      break;
    case FlightBatterySessionState::NeedsConfirmation:
      strncpy(buf, "Needs selection", bufsize);
      break;
    case FlightBatterySessionState::Confirmed: {
      uint8_t cellCount = config.cellCount;
      int16_t capacity = config.capacity;
      if (config.selectedPackSlot > 0) {
        uint8_t slot = config.selectedPackSlot - 1;
        if (slot < MAX_BATTERY_PACKS &&
            g_eeGeneral.batteryPacks[slot].active) {
          cellCount = g_eeGeneral.batteryPacks[slot].cellCount;
          capacity = g_eeGeneral.batteryPacks[slot].capacity;
        }
      }
      snprintf(buf, bufsize, "%dS %d confirmed", cellCount, capacity);
      break;
    }
    case FlightBatterySessionState::ConfirmedWaitingForVoltage:
      strncpy(buf, "Confirmed, voltage lost", bufsize);
      break;
    case FlightBatterySessionState::VoltageMismatch:
      strncpy(buf, "Voltage mismatch", bufsize);
      break;
    case FlightBatterySessionState::NeedsConfiguration:
      strncpy(buf, "Monitor needs setup", bufsize);
      break;
    default:
      strncpy(buf, "Unknown state", bufsize);
      break;
  }
}

class CompatiblePacksMatrix : public ButtonMatrix
{
 public:
  CompatiblePacksMatrix(Window* parent, const rect_t& rect, uint8_t monitor)
      : ButtonMatrix(parent, rect), monitor(monitor)
  {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_BATTERY_PACKS; i++) {
      if (g_eeGeneral.batteryPacks[i].active) {
        count++;
      }
    }
    initBtnMap(4, count);
    update();
  }

  void onPress(uint8_t btn_id) override
  {
    uint8_t slot = getSlotFromBtnId(btn_id);
    if (slot >= MAX_BATTERY_PACKS) return;
    uint16_t mask = g_model.batteryMonitors[monitor].compatiblePackMask;
    mask ^= (1 << slot);
    g_model.batteryMonitors[monitor].compatiblePackMask = mask;
    invalidateFlightBatteryMonitor(monitor);
    SET_DIRTY();
    setChecked(btn_id);
  }

  bool isActive(uint8_t btn_id) override
  {
    uint8_t slot = getSlotFromBtnId(btn_id);
    if (slot >= MAX_BATTERY_PACKS) return false;
    uint16_t mask = g_model.batteryMonitors[monitor].compatiblePackMask;
    return (mask & (1 << slot)) != 0;
  }

  void update()
  {
    uint8_t btn_id = 0;
    for (uint8_t i = 0; i < MAX_BATTERY_PACKS; i++) {
      if (g_eeGeneral.batteryPacks[i].active) {
        const BatteryPackData& pack = g_eeGeneral.batteryPacks[i];
        char label[32];
        snprintf(label, sizeof(label), "%d: %dS %dmAh", i + 1,
                 pack.cellCount, pack.capacity);
        setText(btn_id, label);
        setChecked(btn_id);
        btn_id++;
      }
    }
    ButtonMatrix::update();
  }

 protected:
  uint8_t monitor;

  uint8_t getSlotFromBtnId(uint8_t btn_id)
  {
    uint8_t btn_count = 0;
    for (uint8_t i = 0; i < MAX_BATTERY_PACKS; i++) {
      if (g_eeGeneral.batteryPacks[i].active) {
        if (btn_count == btn_id) return i;
        btn_count++;
      }
    }
    return MAX_BATTERY_PACKS;
  }
};

BatteryMonitorPage::BatteryMonitorPage(uint8_t monitor)
    : SubPage(ICON_MODEL_TELEMETRY, STR_MAIN_MENU_MODEL_SETTINGS,
              (std::string("Battery ") + std::to_string(monitor + 1)).c_str())
{
  body->setFlexLayout();

  BatteryMonitorData* config = &g_model.batteryMonitors[monitor];

  setupLine(nullptr, [=](Window* parent, coord_t x, coord_t y) {
    new StaticText(parent, {x, y, 0, 0}, "Enabled",
                  COLOR_THEME_PRIMARY1_INDEX);
    new ToggleSwitch(parent, {x + 80, y, 0, 0}, GET_DEFAULT(config->enabled),
                      [=](int32_t newValue) {
                        config->enabled = newValue;
                        invalidateFlightBatteryMonitor(monitor);
                        SET_DIRTY();
                      });
  });

  setupLine(STR_TYPE, [=](Window* parent, coord_t x, coord_t y) {
    new Choice(parent, {x, y, 0, 0}, batteryTypeNames, 0, BATTERY_TYPE_LAST - 1,
                GET_DEFAULT(config->batteryType), [=](int32_t newValue) {
                  config->batteryType = newValue;
                  invalidateFlightBatteryMonitor(monitor);
                  SET_DIRTY();
                });
  });

  setupLine(nullptr, [=](Window* parent, coord_t x, coord_t y) {
    new StaticText(parent, {x, y, 0, 0}, "Cells",
                   COLOR_THEME_PRIMARY1_INDEX);
    auto cellsEdit = new NumberEdit(
        parent, {x + 80, y, 0, 0}, 0, 12, GET_DEFAULT(config->cellCount),
        [=](int32_t newValue) {
          config->cellCount = newValue;
          invalidateFlightBatteryMonitor(monitor);
          SET_DIRTY();
        });
    cellsEdit->setAccelFactor(0);
  });

  setupLine(nullptr, [=](Window* parent, coord_t x, coord_t y) {
    new StaticText(parent, {x, y, 0, 0}, "Capacity",
                   COLOR_THEME_PRIMARY1_INDEX);
    auto capacityEdit = new NumberEdit(
        parent, {x + 80, y, 0, 0}, 100, 30000, GET_DEFAULT(config->capacity),
        [=](int32_t newValue) {
          config->capacity = newValue;
          invalidateFlightBatteryMonitor(monitor);
          SET_DIRTY();
        });
    capacityEdit->setStep(100);
    capacityEdit->setAccelFactor(0);
    new StaticText(parent, {x + 160, y, 0, 0}, "mAh");
  });

  setupLine(nullptr, [=](Window* parent, coord_t x, coord_t y) {
    new StaticText(parent, {x, y, 0, 0}, "Voltage Alert",
                  COLOR_THEME_PRIMARY1_INDEX);
    new ToggleSwitch(parent, {x + 100, y, 0, 0},
                     GET_DEFAULT(config->voltAlertEnabled),
                      [=](int32_t newValue) {
                        config->voltAlertEnabled = newValue;
                        invalidateFlightBatteryMonitor(monitor);
                        SET_DIRTY();
                      });
  });

  setupLine(nullptr, [=](Window* parent, coord_t x, coord_t y) {
    new StaticText(parent, {x, y, 0, 0}, "Capacity Alert",
                  COLOR_THEME_PRIMARY1_INDEX);
    new ToggleSwitch(parent, {x + 110, y, 0, 0},
                     GET_DEFAULT(config->capAlertEnabled),
                      [=](int32_t newValue) {
                        config->capAlertEnabled = newValue;
                        invalidateFlightBatteryMonitor(monitor);
                        SET_DIRTY();
                      });
  });

  setupLine(nullptr, [=](Window* parent, coord_t x, coord_t y) {
    new StaticText(parent, {x, y, 0, 0}, "Flight Pack Status",
                   COLOR_THEME_PRIMARY1_INDEX);
  });

  setupLine(nullptr, [=](Window* parent, coord_t x, coord_t y) {
    new DynamicText(parent, {x, y, 0, 0}, [=]() {
      char buf[64];
      auto state = flightBatterySessionState(monitor);
      getFlightPackStatusString(state, monitor, buf, sizeof(buf));
      return std::string(buf);
    });
  });

  bool hasActivePacks = false;
  for (uint8_t i = 0; i < MAX_BATTERY_PACKS; i++) {
    if (g_eeGeneral.batteryPacks[i].active) {
      hasActivePacks = true;
      break;
    }
  }

  if (hasActivePacks) {
    setupLine(nullptr, [=](Window* parent, coord_t x, coord_t y) {
      new StaticText(parent, {x, y, 0, 0}, "Compatible Packs",
                     COLOR_THEME_PRIMARY1_INDEX);
    });

    setupLine(nullptr, [=](Window* parent, coord_t x, coord_t y) {
      new CompatiblePacksMatrix(parent, {x, y, LCD_W - x * 2, 40}, monitor);
    });
  } else {
    setupLine(nullptr, [=](Window* parent, coord_t x, coord_t y) {
      new StaticText(parent, {x, y, 0, 0},
                     "No battery packs configured in Radio Setup",
                     COLOR_THEME_SECONDARY1_INDEX);
    });
  }

  setupLine(nullptr, [=](Window* parent, coord_t x, coord_t y) {
    new StaticText(parent, {x, y, 0, 0}, "Advanced Telemetry",
                   COLOR_THEME_PRIMARY1_INDEX);
  });

  setupLine(STR_VOLTAGE, [=](Window* parent, coord_t x, coord_t y) {
    bool hasVoltageSensor = false;
    for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
      if (isBatteryMonitorVoltageSensor(i)) {
        hasVoltageSensor = true;
        break;
      }
    }
    if (!hasVoltageSensor) {
      new StaticText(parent, {x, y, 0, 0},
                     "No voltage telemetry sensor",
                     COLOR_THEME_WARNING_INDEX);
    } else {
      auto sc = new SourceChoice(
          parent, {x, y, 0, 0}, MIXSRC_NONE, MIXSRC_LAST_TELEM,
          [=]() -> int {
            if (config->sourceIndex <= 0) return MIXSRC_NONE;
            return MIXSRC_FIRST_TELEM + 3 * (config->sourceIndex - 1);
          },
          [=](int newValue) {
            config->sourceIndex =
                newValue == MIXSRC_NONE
                    ? 0
                    : (newValue - MIXSRC_FIRST_TELEM) / 3 + 1;
            invalidateFlightBatteryMonitor(monitor);
            SET_DIRTY();
          });
      sc->setAvailableHandler([=](int16_t value) {
        return isBatteryMonitorSensorSource(value, UNIT_VOLTS);
      });
    }
  });

  setupLine(STR_CURRENTSENSOR, [=](Window* parent, coord_t x, coord_t y) {
    bool hasCurrentSensor = false;
    for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
      if (isBatteryMonitorCapacitySensor(i)) {
        hasCurrentSensor = true;
        break;
      }
    }
    if (!hasCurrentSensor) {
      new StaticText(parent, {x, y, 0, 0},
                      "No capacity telemetry sensor",
                     COLOR_THEME_WARNING_INDEX);
    } else {
      auto sc = new SourceChoice(
          parent, {x, y, 0, 0}, MIXSRC_NONE, MIXSRC_LAST_TELEM,
          [=]() -> int {
            if (config->currentIndex <= 0) return MIXSRC_NONE;
            return MIXSRC_FIRST_TELEM + 3 * (config->currentIndex - 1);
          },
          [=](int newValue) {
            config->currentIndex =
                newValue == MIXSRC_NONE
                    ? 0
                    : (newValue - MIXSRC_FIRST_TELEM) / 3 + 1;
            invalidateFlightBatteryMonitor(monitor);
            SET_DIRTY();
          });
      sc->setAvailableHandler([=](int16_t value) {
        return isBatteryMonitorSensorSource(value, UNIT_MAH);
      });
    }
  });
}
