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

#include "battery_packs.h"

#include "button.h"
#include "choice.h"
#include "edgetx.h"
#include "getset_helpers.h"
#include "numberedit.h"
#include "page.h"
#include "telemetry/battery_monitor.h"

#define SET_DIRTY() storageDirty(EE_GENERAL)

static constexpr coord_t ROW_H = EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_TINY;
static constexpr uint16_t DEFAULT_BATTERY_PACK_CAPACITY_MAH = 2200;
static constexpr uint16_t MIN_BATTERY_PACK_CAPACITY_MAH = 100;

static bool batterySpecExists(uint8_t excludeSlot, uint8_t type,
                               uint8_t cells, int16_t cap)
{
  for (uint8_t i = 0; i < MAX_BATTERY_PACKS; i++) {
    if (i == excludeSlot) continue;
    const auto& p = g_eeGeneral.batteryPacks[i];
    if (!p.active) continue;
    if ((uint8_t)p.batteryType == type && p.cellCount == cells && p.capacity == cap)
      return true;
  }
  return false;
}

class BatteryPackButton : public Button
{
 public:
  BatteryPackButton(BatteryPacksPage* page, Window* parent, uint8_t slot) :
      Button(parent, {0, 0, LCD_W, ROW_H}), page(page), slot(slot)
  {
    padAll(PAD_TINY);
    setPressHandler([=]() -> uint8_t {
      page->editPack(slot);
      return 0;
    });
    build();
  }

  void build()
  {
    BatteryPackData* pack = &g_eeGeneral.batteryPacks[slot];

    coord_t y = height() / 2 - EdgeTxStyles::STD_FONT_HEIGHT / 2;

    char spec[48];
    snprintf(spec, sizeof(spec), "%s %dS %dmAh",
             batteryTypeToString((BatteryType)pack->batteryType),
             pack->cellCount, pack->capacity);
    new StaticText(this, {PAD_MEDIUM, y, LCD_W - PAD_MEDIUM * 2,
                          EdgeTxStyles::STD_FONT_HEIGHT},
                   spec, COLOR_THEME_PRIMARY1_INDEX);
  }

 protected:
  BatteryPacksPage* page;
  uint8_t slot;
};

BatteryPacksPage::BatteryPacksPage() :
    SubPage(ICON_RADIO, STR_MAIN_MENU_RADIO_SETTINGS, STR_BATTERY_PACKS)
{
  body->padAll(PAD_SMALL);
  body->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_TINY);
  build();
}

void BatteryPacksPage::build()
{
  for (uint8_t slot = 0; slot < MAX_BATTERY_PACKS; slot++) {
    BatteryPackData* pack = &g_eeGeneral.batteryPacks[slot];
    if (pack->active) {
      new BatteryPackButton(this, body, slot);
    }
  }

  uint8_t firstInactive = MAX_BATTERY_PACKS;
  for (uint8_t slot = 0; slot < MAX_BATTERY_PACKS; slot++) {
    if (!g_eeGeneral.batteryPacks[slot].active) {
      firstInactive = slot;
      break;
    }
  }

  if (firstInactive < MAX_BATTERY_PACKS) {
    new TextButton(body, {0, 0, LCD_W, ROW_H}, "+", [=]() -> uint8_t {
      BatteryPackData* pack = &g_eeGeneral.batteryPacks[firstInactive];

      pack->active = 1;
      pack->batteryType = BATTERY_TYPE_LIPO;
      pack->cellCount = 3;
      pack->capacity = DEFAULT_BATTERY_PACK_CAPACITY_MAH;
      SET_DIRTY();
      editPack(firstInactive);
      return 0;
    });
  }
}

void BatteryPacksPage::rebuild()
{
  body->clear();
  build();
}

void BatteryPacksPage::editPack(uint8_t slot)
{
  auto editWindow = new BatteryPackEditWindow(slot);
  editWindow->setCloseHandler([=]() { rebuild(); });
}

#if defined(COLORLCD)
static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(2),
                                     LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
#endif

static const char* batteryTypeLabels[] = {
  "LiPo", "Li-Ion", "LiFe", "NiMH", "Pb"
};

class BatteryPackEditBody : public Window
{
 public:
  BatteryPackEditBody(BatteryPackEditWindow* page, Window* parent, coord_t height,
                      uint8_t slot);

 protected:
  BatteryPackEditWindow* page;
  uint8_t slot;
};

BatteryPackEditBody::BatteryPackEditBody(BatteryPackEditWindow* page,
                                         Window* parent, coord_t height,
                                         uint8_t slot) :
    Window(parent, {0, 0, LCD_W, height}), page(page), slot(slot)
{
  padAll(PAD_MEDIUM);

#if defined(COLORLCD)
  FlexGridLayout grid(col_dsc, row_dsc, PAD_TINY);
  setFlexLayout();

  BatteryPackData* pack = &g_eeGeneral.batteryPacks[slot];

  auto typeLine = newLine(grid);
  new StaticText(typeLine, rect_t{}, "Chemistry");
  new Choice(typeLine, rect_t{}, batteryTypeLabels,
                             0, BATTERY_TYPE_PB,
                             GET_DEFAULT(pack->batteryType),
                             [=](int32_t newValue) {
                               if (batterySpecExists(slot, newValue,
                                   pack->cellCount, pack->capacity))
                                 return;
                               pack->batteryType = (BatteryType)newValue;
                               invalidateFlightBatteryPackSlot(slot);
                               SET_DIRTY();
                             });

  auto cellsLine = newLine(grid);
  new StaticText(cellsLine, rect_t{}, "Cells");
  auto cellsEdit = new NumberEdit(cellsLine, rect_t{}, 1, 12,
                                  GET_DEFAULT(pack->cellCount),
                                  [=](int32_t newValue) {
                                    if (batterySpecExists(slot,
                                        pack->batteryType, newValue,
                                        pack->capacity))
                                      return;
                                    pack->cellCount = newValue;
                                    invalidateFlightBatteryPackSlot(slot);
                                    SET_DIRTY();
                                  });
  cellsEdit->setAccelFactor(0);

  auto capLine = newLine(grid);
  new StaticText(capLine, rect_t{}, "Capacity");
  auto capEdit = new NumberEdit(capLine, rect_t{},
                                 MIN_BATTERY_PACK_CAPACITY_MAH, 30000,
                                 GET_DEFAULT(pack->capacity),
                                 [=](int32_t newValue) {
                                   if (batterySpecExists(slot,
                                       pack->batteryType, pack->cellCount,
                                       newValue))
                                     return;
                                   pack->capacity = newValue;
                                   invalidateFlightBatteryPackSlot(slot);
                                   SET_DIRTY();
                                 });
  capEdit->setSuffix("mAh");
  capEdit->setStep(100);
  capEdit->setAccelFactor(0);

  auto delLine = newLine(grid);
  new TextButton(delLine, rect_t{}, STR_DELETE, [=]() -> uint8_t {
    invalidateFlightBatteryPackSlot(slot);
    pack->active = 0;
    uint16_t slotBit = (1u << slot);
    for (uint8_t i = 0; i < MAX_BATTERY_MONITORS; i++) {
      g_model.batteryMonitors[i].compatiblePackMask &= ~slotBit;
    }
    SET_DIRTY();
    storageDirty(EE_MODEL);
    page->deleteLater();
    return 0;
  });
#else
  (void)height;
#endif
}

BatteryPackEditWindow::BatteryPackEditWindow(uint8_t slot) :
    Page(ICON_RADIO), slot(slot)
{
  header->setTitle(STR_BATTERY_PACKS);
  header->setTitle2(std::to_string(slot + 1));

  new BatteryPackEditBody(this, body, body->height(), slot);
}
