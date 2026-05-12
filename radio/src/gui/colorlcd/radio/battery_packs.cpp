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
#include "edgetx.h"
#include "getset_helpers.h"
#include "numberedit.h"
#include "page.h"
#include "telemetry/battery_monitor.h"
#include "textedit.h"

#define SET_DIRTY() storageDirty(EE_GENERAL)

static constexpr coord_t ROW_H = EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_TINY;
static constexpr uint16_t DEFAULT_BATTERY_PACK_CAPACITY_MAH = 2200;
static constexpr uint16_t MIN_BATTERY_PACK_CAPACITY_MAH = 100;

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

    new StaticText(this, {PAD_MEDIUM, y, 30, EdgeTxStyles::STD_FONT_HEIGHT},
                   std::to_string(slot + 1).c_str(), COLOR_THEME_SECONDARY1_INDEX);

    char name[LEN_BATTERY_PACK_NAME + 1] = {};
    strncpy(name, pack->name, LEN_BATTERY_PACK_NAME);
    new StaticText(this, {PAD_MEDIUM + 35, y, 80, EdgeTxStyles::STD_FONT_HEIGHT},
                   name, COLOR_THEME_PRIMARY1_INDEX);

    char cells[8];
    snprintf(cells, sizeof(cells), "%dS", pack->cellCount);
    new StaticText(this, {PAD_MEDIUM + 120, y, 30, EdgeTxStyles::STD_FONT_HEIGHT},
                   cells, COLOR_THEME_PRIMARY1_INDEX);

    char cap[16];
    snprintf(cap, sizeof(cap), "%dmAh", pack->capacity);
    new StaticText(this, {PAD_MEDIUM + 155, y, 70, EdgeTxStyles::STD_FONT_HEIGHT},
                   cap, COLOR_THEME_PRIMARY1_INDEX);
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
      pack->name[0] = '\0';
      pack->capacity = DEFAULT_BATTERY_PACK_CAPACITY_MAH;
      pack->cellCount = 3;
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

  auto nameLine = newLine(grid);
  new StaticText(nameLine, rect_t{}, STR_NAME);
  new RadioTextEdit(nameLine, rect_t{}, pack->name, LEN_BATTERY_PACK_NAME);

  auto cellsLine = newLine(grid);
  new StaticText(cellsLine, rect_t{}, "Cells");
  auto cellsEdit = new NumberEdit(cellsLine, rect_t{}, 1, 12,
                                  GET_DEFAULT(pack->cellCount),
                                  [=](int32_t newValue) {
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
