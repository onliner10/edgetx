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

#include "view_logical_switches.h"

#include <new>

#include "button.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "quick_menu.h"
#include "static.h"
#include "switches.h"

#if PORTRAIT

// Footer grid
static const lv_coord_t f_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1),
                                       LV_GRID_FR(1), LV_GRID_FR(1),
                                       LV_GRID_TEMPLATE_LAST};

#else

// Footer grid
LAYOUT_VAL_SCALED(LS_C1, 60)
LAYOUT_VAL_SCALED(LS_C3, 112)
LAYOUT_VAL_SCALED(LS_C5, 50)
static const lv_coord_t f_col_dsc[] = {
    LS_C1, LV_GRID_FR(1),        LS_C3, LV_GRID_FR(1), LS_C5,
    LS_C5, LV_GRID_TEMPLATE_LAST};

#endif

static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT,
                                     LV_GRID_TEMPLATE_LAST};

void getsEdgeDelayParam(char* s, LogicalSwitchData* ls);

class LogicalSwitchDisplayFooter : public Window
{
 public:
  explicit LogicalSwitchDisplayFooter(Window* parent, rect_t rect) :
      Window(parent, rect)
  {
    setWindowFlag(OPAQUE);

    padAll(PAD_ZERO);
    padLeft(PAD_SMALL);
    padRight(PAD_SMALL);

    if (withLive([&](LiveWindow& live) {
          auto obj = live.lvobj();
          etx_solid_bg(obj, COLOR_THEME_SECONDARY1_INDEX);

          lv_obj_set_layout(obj, LV_LAYOUT_GRID);
          lv_obj_set_grid_dsc_array(obj, f_col_dsc, row_dsc);
          lv_obj_set_style_pad_row(obj, 0, 0);
          lv_obj_set_style_pad_column(obj, PAD_TINY, 0);

          auto createLabel = [&](uint8_t col, uint8_t colCnt, uint8_t row,
                                 lv_obj_t*& label) {
            label = etx_label_create(obj);
            if (!requireLvObj(label)) return false;
            etx_obj_add_style(label, styles->text_align_left, LV_PART_MAIN);
            etx_txt_color(label, COLOR_THEME_PRIMARY2_INDEX);
            lv_obj_set_grid_cell(label, LV_GRID_ALIGN_STRETCH, col, colCnt,
                                 LV_GRID_ALIGN_CENTER, row, 1);
            return true;
          };

          if (!createLabel(0, 1, 0, lsFunc)) return false;
          if (!createLabel(1, 1, 0, lsV1)) return false;
          if (!createLabel(2, V2_COL_CNT, 0, lsV2)) return false;
          if (!createLabel(ANDSW_COL, 1, ANDSW_ROW, lsAnd)) return false;
          if (!createLabel(ANDSW_COL + 1, 1, ANDSW_ROW, lsDuration))
            return false;
          if (!createLabel(ANDSW_COL + 2, 1, ANDSW_ROW, lsDelay)) return false;

          if (parent) {
            parent->withLive([](Window::LiveWindow& liveParent) {
              lv_obj_update_layout(liveParent.lvobj());
            });
          }
          return true;
        })) {
      refresh();
    }
  }

  void refresh()
  {
    withLive([&](LiveWindow&) {
      if (!lsFunc || !lsV1 || !lsV2 || !lsAnd || !lsDuration || !lsDelay)
        return;

      LogicalSwitchData* ls = lswAddress(lsIndex);
      uint8_t lsFamily = lswFamily(ls->func);

      char s[20] = "";

      lv_label_set_text(lsFunc, STR_VCSWFUNC[ls->func]);

      // CSW params - V1
      switch (lsFamily) {
        case LS_FAMILY_BOOL:
        case LS_FAMILY_STICKY:
        case LS_FAMILY_EDGE:
          lv_label_set_text(lsV1, getSwitchPositionName(ls->v1));
          break;
        case LS_FAMILY_TIMER:
          lv_label_set_text(lsV1, formatNumberAsString(lswTimerValue(ls->v1),
                                                       PREC1, 0, nullptr, "s")
                                      .c_str());
          break;
        default:
          lv_label_set_text(lsV1, getSourceString(ls->v1));
          break;
      }

      // CSW params - V2
      switch (lsFamily) {
        case LS_FAMILY_BOOL:
        case LS_FAMILY_STICKY:
          lv_label_set_text(lsV2, getSwitchPositionName(ls->v2));
          break;
        case LS_FAMILY_EDGE:
          getsEdgeDelayParam(s, ls);
          lv_label_set_text(lsV2, s);
          break;
        case LS_FAMILY_TIMER:
          lv_label_set_text(lsV2, formatNumberAsString(lswTimerValue(ls->v2),
                                                       PREC1, 0, nullptr, "s")
                                      .c_str());
          break;
        case LS_FAMILY_COMP:
          lv_label_set_text(lsV2, getSourceString(ls->v2));
          break;
        default:
          lv_label_set_text(
              lsV2,
              getSourceCustomValueString(
                  ls->v1,
                  (ls->v1 <= MIXSRC_LAST_CH ? calc100toRESX(ls->v2) : ls->v2),
                  0));
          break;
      }

      // AND switch
      lv_label_set_text(lsAnd, getSwitchPositionName(ls->andsw));

      // CSW duration
      if (ls->duration > 0) {
        lv_label_set_text(
            lsDuration,
            formatNumberAsString(ls->duration, PREC1, 0, nullptr, "s").c_str());
      } else {
        lv_label_set_text(lsDuration, "");
      }

      // CSW delay
      if (lsFamily != LS_FAMILY_EDGE && ls->delay > 0) {
        lv_label_set_text(
            lsDelay,
            formatNumberAsString(ls->delay, PREC1, 0, nullptr, "s").c_str());
      } else {
        lv_label_set_text(lsDelay, "");
      }
    });
  }

  void setIndex(unsigned value)
  {
    lsIndex = value;
    refresh();
  }

 static LAYOUT_ORIENTATION(V2_COL_CNT, 1, 2) static LAYOUT_ORIENTATION(
     ANDSW_ROW, 0, 1) static LAYOUT_ORIENTATION(ANDSW_COL, 3, 1)

     protected : unsigned lsIndex = 0;
  lv_obj_t* lsFunc = nullptr;
  lv_obj_t* lsV1 = nullptr;
  lv_obj_t* lsV2 = nullptr;
  lv_obj_t* lsAnd = nullptr;
  lv_obj_t* lsDuration = nullptr;
  lv_obj_t* lsDelay = nullptr;
};

LogicalSwitchesViewPage::LogicalSwitchesViewPage() :
    PageGroupItem(STR_MONITOR_SWITCHES, QM_TOOLS_LS_MON)
{
  setIcon(ICON_MONITOR_LOGICAL_SWITCHES);
}

LogicalSwitchesViewPage::LogicalSwitchesViewPage(const PageDef& pageDef) :
    PageGroupItem(pageDef)
{
}

void LogicalSwitchesViewPage::build(Window* window)
{
  window->padAll(PAD_ZERO);

  coord_t xo =
      (LCD_W - (BTN_MATRIX_COL * (BTN_WIDTH + PAD_TINY) - PAD_TINY)) / 2;
  coord_t yo = PAD_TINY;

  // Footer
  footer = new (std::nothrow) LogicalSwitchDisplayFooter(
      window,
      {0, window->height() - FOOTER_HEIGHT, window->width(), FOOTER_HEIGHT});
  if (!footer) return;

  int btnHeight =
      (window->height() - FOOTER_HEIGHT) /
          ((MAX_LOGICAL_SWITCHES + BTN_MATRIX_COL - 1) / BTN_MATRIX_COL) -
      PAD_TINY;

  // LSW table
  std::string lsString("L64");
  for (uint8_t i = 0; i < MAX_LOGICAL_SWITCHES; i++) {
    coord_t x = (i % BTN_MATRIX_COL) * (BTN_WIDTH + PAD_TINY) + xo;
    coord_t y = (i / BTN_MATRIX_COL) * (btnHeight + PAD_TINY) + yo;

    LogicalSwitchData* ls = lswAddress(i);
    bool isActive = (ls->func != LS_FUNC_NONE);

    strAppendSigned(&lsString[1], i + 1, 2);

    if (isActive) {
      auto button = new (std::nothrow)
          TextButton(window, {x, y, BTN_WIDTH, btnHeight}, lsString);
      if (!button) continue;
      button->setCheckHandler(
          [=]() { button->check(getSwitch(SWSRC_FIRST_LOGICAL_SWITCH + i)); });
      button->setFocusHandler([=](bool focus) {
        if (focus) {
          footer->setIndex(i);
        }
      });
    } else {
      if (btnHeight > EdgeTxStyles::STD_FONT_HEIGHT)
        y += (btnHeight - EdgeTxStyles::STD_FONT_HEIGHT) / 2;
      new (std::nothrow)
          StaticText(window, {x, y, BTN_WIDTH, btnHeight}, lsString,
                     COLOR_THEME_DISABLED_INDEX, CENTERED);
    }
  }
}
