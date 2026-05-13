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

#include "edgetx.h"
#include "getset_helpers.h"
#include "sourcechoice.h"
#include "list_line_button.h"
#include "toggleswitch.h"
#include "telemetry/battery_monitor.h"

#define SET_DIRTY() storageDirty(EE_MODEL)

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

  if (config.batteryType > BATTERY_TYPE_PB) {
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
      BatteryType chem = (BatteryType)config.batteryType;
      if (config.selectedPackSlot > 0) {
        uint8_t slot = config.selectedPackSlot - 1;
        if (slot < MAX_BATTERY_PACKS &&
            g_eeGeneral.batteryPacks[slot].active) {
          cellCount = g_eeGeneral.batteryPacks[slot].cellCount;
          capacity = g_eeGeneral.batteryPacks[slot].capacity;
          chem = (BatteryType)g_eeGeneral.batteryPacks[slot].batteryType;
        }
      }
      snprintf(buf, bufsize, "%s %dS %d confirmed",
               batteryTypeToString(chem), cellCount, capacity);
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

class CompatiblePackLine : public ListLineButton
{
 public:
  CompatiblePackLine(Window* parent, uint8_t monitor, uint8_t slot)
      : ListLineButton(parent, slot), monitor(monitor), slot(slot)
  {
    setHeight(PACK_LINE_H);
    padAll(PAD_ZERO);
    setWidth(ListLineButton::GRP_W);

    nameText = new StaticText(this, {PAD_MEDIUM, PACK_TEXT_Y,
                                    ListLineButton::GRP_W - 2 * PAD_MEDIUM,
                                    EdgeTxStyles::STD_FONT_HEIGHT},
                             "", COLOR_THEME_PRIMARY1_INDEX);

    setPressHandler([this]() -> uint8_t {
      togglePack();
      return 0;
    });
    check(isActive());
    delayLoad();
  }

  bool isActive() const override
  {
    const uint16_t mask = g_model.batteryMonitors[monitor].compatiblePackMask;
    return (mask & specMask()) != 0;
  }

  void onRefresh() override
  {
    if (!nameText) {
      return;
    }

    const BatteryPackData& pack = g_eeGeneral.batteryPacks[slot];
    char lineText[64] = {};
    snprintf(lineText, sizeof(lineText), "%s %dS %dmAh",
             batteryTypeToString((BatteryType)pack.batteryType),
             pack.cellCount, pack.capacity);
    nameText->setText(lineText);
    check(isActive());
  }

  void delayedInit() override { refresh(); }

 protected:
  uint8_t monitor;
  uint8_t slot;
  StaticText* nameText = nullptr;

  static constexpr coord_t PACK_LINE_H = EdgeTxStyles::UI_ELEMENT_HEIGHT +
                                         PAD_TINY * 2;
  static constexpr coord_t PACK_TEXT_Y =
      (PACK_LINE_H - EdgeTxStyles::STD_FONT_HEIGHT) / 2;

  uint16_t specMask() const
  {
    uint16_t mask = 0;
    const auto& ref = g_eeGeneral.batteryPacks[slot];
    for (uint8_t i = 0; i < MAX_BATTERY_PACKS; i++) {
      const auto& p = g_eeGeneral.batteryPacks[i];
      if (!p.active) continue;
      if (batterySpecEquals(p, ref))
        mask |= (uint16_t(1u) << i);
    }
    return mask;
  }

  void togglePack()
  {
    uint16_t mask = g_model.batteryMonitors[monitor].compatiblePackMask;
    uint16_t match = specMask();
    if ((mask & match) == match)
      mask &= ~match;
    else
      mask |= match;
    g_model.batteryMonitors[monitor].compatiblePackMask = mask;
    invalidateFlightBatteryMonitor(monitor);
    SET_DIRTY();
    check(isActive());
  }
};

BatteryMonitorPage::BatteryMonitorPage(uint8_t monitor)
    : SubPage(ICON_MODEL_TELEMETRY, STR_MAIN_MENU_MODEL_SETTINGS,
              (std::string("Battery ") + std::to_string(monitor + 1)).c_str())
{
  body->setFlexLayout();

  BatteryMonitorData* config = &g_model.batteryMonitors[monitor];

  setupLine("Enabled", [=](Window* parent, coord_t x, coord_t y) {
    new ToggleSwitch(parent, {x, y, 0, 0}, GET_DEFAULT(config->enabled),
                      [=](int32_t newValue) {
                        config->enabled = newValue;
                        invalidateFlightBatteryMonitor(monitor);
                        SET_DIRTY();
                      });
  });

  setupLine("Voltage Alert", [=](Window* parent, coord_t x, coord_t y) {
    new ToggleSwitch(parent, {x, y, 0, 0},
                     GET_DEFAULT(config->voltAlertEnabled),
                      [=](int32_t newValue) {
                        config->voltAlertEnabled = newValue;
                        invalidateFlightBatteryMonitor(monitor);
                        SET_DIRTY();
                      });
  });

  setupLine("Capacity Alert", [=](Window* parent, coord_t x, coord_t y) {
    new ToggleSwitch(parent, {x, y, 0, 0},
                     GET_DEFAULT(config->capAlertEnabled),
                      [=](int32_t newValue) {
                        config->capAlertEnabled = newValue;
                        invalidateFlightBatteryMonitor(monitor);
                        SET_DIRTY();
                      });
  });

  setupLine("Flight Pack Status", [=](Window* parent, coord_t x, coord_t y) {
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
    setupLine("Compatible Batteries", nullptr);

    bool seen[MAX_BATTERY_PACKS] = {};
    for (uint8_t i = 0; i < MAX_BATTERY_PACKS; i++) {
      if (!g_eeGeneral.batteryPacks[i].active) continue;
      if (seen[i]) continue;

      for (uint8_t j = i + 1; j < MAX_BATTERY_PACKS; j++) {
        if (g_eeGeneral.batteryPacks[j].active &&
            batterySpecEquals(g_eeGeneral.batteryPacks[i],
                              g_eeGeneral.batteryPacks[j]))
          seen[j] = true;
      }

      uint8_t slotIdx = i;
      setupLine(nullptr, [=](Window* parent, coord_t, coord_t) {
        new CompatiblePackLine(parent, monitor, slotIdx);
      });
    }
  } else {
    setupLine("No batteries configured in Radio Setup", nullptr);
  }

  setupLine("Advanced Telemetry", nullptr);

  setupLine(STR_VOLTAGE, [=](Window* parent, coord_t x, coord_t y) {
    bool hasVoltageSensor = false;
    for (int i = 0; i < MAX_TELEMETRY_SENSORS; i++) {
      if (isBatteryMonitorVoltageSensor(i)) {
        hasVoltageSensor = true;
        break;
      }
    }
    if (!hasVoltageSensor) {
      new StaticText(parent, {x, y + PAD_LARGE, 0, 0},
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
      new StaticText(parent, {x, y + PAD_LARGE, 0, 0},
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
