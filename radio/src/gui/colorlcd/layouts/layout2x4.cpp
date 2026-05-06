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

#include "layout.h"
#include "translations/translations.h"

const LayoutOption OPTIONS_LAYOUT_2x4[] = {
    LAYOUT_COMMON_OPTIONS,
    {STR_DEF(STR_PANEL1_BACKGROUND), LayoutOption::Bool, true},
    {STR_DEF(STR_PANEL_COLOR), LayoutOption::Color, RGB2FLAGS(77, 112, 203)},
    {STR_DEF(STR_PANEL2_BACKGROUND), LayoutOption::Bool, true},
    {STR_DEF(STR_PANEL_COLOR), LayoutOption::Color, RGB2FLAGS(77, 112, 203)},
    LAYOUT_OPTIONS_END};

class Layout2x4 : public Layout
{
 public:
  enum {
    OPTION_PANEL1_BACKGROUND = LAYOUT_OPTION_LAST_DEFAULT + 1,
    OPTION_PANEL1_COLOR,
    OPTION_PANEL2_BACKGROUND,
    OPTION_PANEL2_COLOR
  };

  Layout2x4(Window* parent, const LayoutFactory* factory, int screenNum,
            uint8_t zoneCount, uint8_t* zoneMap) :
      Layout(parent, factory, screenNum, zoneCount, zoneMap)
  {
    withLive([&](LiveWindow& live) {
      auto obj1 = lv_obj_create(live.lvobj());
      if (!requireLvObj(panel1, obj1)) return false;
      lv_obj_set_style_bg_opa(obj1, LV_OPA_COVER, LV_PART_MAIN);
      auto obj2 = lv_obj_create(live.lvobj());
      if (!requireLvObj(panel2, obj2)) return false;
      lv_obj_set_style_bg_opa(obj2, LV_OPA_COVER, LV_PART_MAIN);
      return true;
    });
    setPanels();
  }

 protected:
  rect_t mainZone = {0, 0, 0, 0};
  RequiredLvObj panel1;
  RequiredLvObj panel2;

  void updateDecorations() override
  {
    Layout::updateDecorations();
    setPanels();
  }

  void setPanels()
  {
    panel1.with([&](lv_obj_t* obj1) {
      panel2.with([&](lv_obj_t* obj2) {
        rect_t zone = Layout::getWidgetsZone();
        if (mainZone.x != zone.x || mainZone.y != zone.y ||
            mainZone.w != zone.w || mainZone.h != zone.h) {
          mainZone = zone;
          lv_obj_set_pos(obj1, mainZone.x, mainZone.y);
          lv_obj_set_size(obj1, mainZone.w / 2, mainZone.h);
          lv_obj_set_pos(obj2, mainZone.x + mainZone.w / 2, mainZone.y);
          lv_obj_set_size(obj2, mainZone.w / 2, mainZone.h);
        }

        bool vis = getOptionValue(OPTION_PANEL1_BACKGROUND)->boolValue;
        if (vis == lv_obj_has_flag(obj1, LV_OBJ_FLAG_HIDDEN)) {
          if (vis)
            lv_obj_clear_flag(obj1, LV_OBJ_FLAG_HIDDEN);
          else
            lv_obj_add_flag(obj1, LV_OBJ_FLAG_HIDDEN);
        }
        vis = getOptionValue(OPTION_PANEL2_BACKGROUND)->boolValue;
        if (vis == lv_obj_has_flag(obj2, LV_OBJ_FLAG_HIDDEN)) {
          if (vis)
            lv_obj_clear_flag(obj2, LV_OBJ_FLAG_HIDDEN);
          else
            lv_obj_add_flag(obj2, LV_OBJ_FLAG_HIDDEN);
        }

        etx_bg_color_from_flags(
            obj1, getOptionValue(OPTION_PANEL1_COLOR)->unsignedValue);
        etx_bg_color_from_flags(
            obj2, getOptionValue(OPTION_PANEL2_COLOR)->unsignedValue);
      });
    });
  }
};

// Zone map: 2x4 (2 columns, 4 rows)
// Each zone is 1/2 width, 1/4 height
// Zones ordered column-by-column (left column first, then right column)
// clang-format off
static const uint8_t zmap[] = {
    // Zone positions: x, y, w, h (using LAYOUT_MAP constants)
    LAYOUT_MAP_0,    LAYOUT_MAP_0,    LAYOUT_MAP_HALF, LAYOUT_MAP_1QTR,  // Left column, top
    LAYOUT_MAP_0,    LAYOUT_MAP_1QTR, LAYOUT_MAP_HALF, LAYOUT_MAP_1QTR,  // Left column, 2nd row
    LAYOUT_MAP_0,    LAYOUT_MAP_HALF, LAYOUT_MAP_HALF, LAYOUT_MAP_1QTR,  // Left column, 3rd row
    LAYOUT_MAP_0,    LAYOUT_MAP_3QTR, LAYOUT_MAP_HALF, LAYOUT_MAP_1QTR,  // Left column, bottom
    LAYOUT_MAP_HALF, LAYOUT_MAP_0,    LAYOUT_MAP_HALF, LAYOUT_MAP_1QTR,  // Right column, top
    LAYOUT_MAP_HALF, LAYOUT_MAP_1QTR, LAYOUT_MAP_HALF, LAYOUT_MAP_1QTR,  // Right column, 2nd row
    LAYOUT_MAP_HALF, LAYOUT_MAP_HALF, LAYOUT_MAP_HALF, LAYOUT_MAP_1QTR,  // Right column, 3rd row
    LAYOUT_MAP_HALF, LAYOUT_MAP_3QTR, LAYOUT_MAP_HALF, LAYOUT_MAP_1QTR,  // Right column, bottom
};
// clang-format on
BaseLayoutFactory<Layout2x4> layout2x4("Layout2x4", "2 x 4", OPTIONS_LAYOUT_2x4,
                                       8, (uint8_t*)zmap);
