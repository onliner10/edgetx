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

#include "widget.h"

#include "bitmaps.h"
#include "edgetx.h"
#include "static.h"

#define ETX_STATE_BG_WARNING LV_STATE_USER_1
#define EXT_NAME_ALIGN_RIGHT LV_STATE_USER_1
#define ETX_NAME_TXT_WARNING LV_STATE_USER_2
#define ETX_NAME_COLOR_WHITE LV_STATE_USER_3
#define ETX_VALUE_SMALL_FONT LV_STATE_USER_1

class TimerWidget : public TrackedWidget
{
 public:
  TimerWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
              WidgetLocation location) :
      TrackedWidget(factory, parent, rect, location, LoadMode::Delayed)
  {
  }

  void delayedInit() override
  {
    solidBg(COLOR_THEME_WARNING_INDEX, LV_PART_MAIN | ETX_STATE_BG_WARNING);

    lv_style_init(&style);
    lv_style_set_width(&style, lv_pct(100));
    lv_style_set_height(&style, LV_SIZE_CONTENT);

    if (!initRequiredWindow(timerBg, this, 0, 0, ICON_TIMER_BG,
                            COLOR_THEME_PRIMARY2_INDEX))
      return;
    if (!initRequiredWindow(timerIcon, this, PAD_THREE, PAD_SMALL, ICON_TIMER,
                            COLOR_THEME_SECONDARY1_INDEX))
      return;

    // Timer name
    initRequiredLvObj(
        nameLabel,
        [](lv_obj_t* parent) { return etx_label_create(parent, FONT_XS_INDEX); },
        [&](lv_obj_t* obj) {
          lv_label_set_text(obj, "");
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_obj_add_style(obj, &style, LV_PART_MAIN);
          etx_obj_add_style(obj, styles->text_align_left, LV_PART_MAIN);
          etx_obj_add_style(obj, styles->text_align_right,
                            LV_PART_MAIN | EXT_NAME_ALIGN_RIGHT);
          etx_txt_color(obj, COLOR_THEME_SECONDARY1_INDEX);
          etx_txt_color(obj, COLOR_THEME_SECONDARY2_INDEX,
                        LV_PART_MAIN | ETX_NAME_TXT_WARNING);
          etx_txt_color(obj, COLOR_THEME_PRIMARY2_INDEX,
                        LV_PART_MAIN | ETX_NAME_COLOR_WHITE);
        });

    // Timer value - on small size widgets
    initRequiredLvObj(
        valLabel, [](lv_obj_t* parent) { return etx_label_create(parent); },
        [&](lv_obj_t* obj) {
          lv_label_set_text(obj, "");
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_obj_add_style(obj, &style, LV_PART_MAIN);
          etx_txt_color(obj, COLOR_THEME_PRIMARY2_INDEX);
          etx_font(obj, FONT_XS_INDEX, LV_PART_MAIN | ETX_VALUE_SMALL_FONT);
          lv_obj_set_pos(obj, PAD_THREE, VAL_LBL_Y);
        });

    // Timer value - on large widgets
    if (!createUnitLabel(unit0)) return;
    unit0.with([](lv_obj_t* obj) { lv_obj_set_pos(obj, U0_X, U0_Y); });
    if (!createUnitLabel(unit1)) return;
    unit1.with([](lv_obj_t* obj) { lv_obj_set_pos(obj, U1_X, U1_Y); });
    if (!createDigitsLabel(digits0)) return;
    digits0.with([](lv_obj_t* obj) { lv_obj_set_pos(obj, D0_X, D0_Y); });
    if (!createDigitsLabel(digits1)) return;
    digits1.with([](lv_obj_t* obj) { lv_obj_set_pos(obj, D1_X, D1_Y); });

    initRequiredLvObj(
        timerArc, [](lv_obj_t* parent) { return lv_arc_create(parent); },
        [](lv_obj_t* obj) {
          lv_arc_set_rotation(obj, 270);
          lv_arc_set_bg_angles(obj, 0, 360);
          lv_arc_set_range(obj, 0, 360);
          lv_arc_set_angles(obj, 0, 360);
          lv_arc_set_start_angle(obj, 0);
          lv_obj_remove_style(obj, NULL, LV_PART_KNOB);
          lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
          lv_obj_set_pos(obj, PAD_TINY, PAD_THREE);
          lv_obj_set_size(obj, TMR_ARC_SZ, TMR_ARC_SZ);
          lv_obj_set_style_arc_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
          lv_obj_set_style_arc_width(obj, TMR_ARC_W, LV_PART_MAIN);
          lv_obj_set_style_arc_opa(obj, LV_OPA_COVER, LV_PART_INDICATOR);
          lv_obj_set_style_arc_width(obj, TMR_ARC_W, LV_PART_INDICATOR);
          etx_arc_color(obj, COLOR_THEME_SECONDARY1_INDEX, LV_PART_INDICATOR);
          lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        });

    update();
    foreground();
  }

  uint32_t refreshKey() override
  {
    auto widgetData = getPersistentData();
    uint32_t index = widgetData->options[0].value.unsignedValue;
    TimerData& timerData = g_model.timers[index];
    TimerState& timerState = timersStates[index];

    WidgetRefreshKey key;
    key.add((uint32_t)index)
       .add((uint32_t)timerData.start)
       .add((bool)timerData.showElapsed)
       .add((int32_t)timerState.val);
    return key.value();
  }

  void refresh() override
  {
    auto widgetData = getPersistentData();

    uint32_t index = widgetData->options[0].value.unsignedValue;
    TimerData& timerData = g_model.timers[index];
    TimerState& timerState = timersStates[index];

    if (lastValue != timerState.val || lastStartValue != timerData.start) {
      lastValue = timerState.val;
      lastStartValue = timerData.start;

      if (lastStartValue && lastValue > 0) {
        int pieEnd = 360 * (lastStartValue - lastValue) / lastStartValue;
        if (!timerData.showElapsed) {
          pieEnd = 360 - pieEnd;
        }
        timerArc.with(
            [&](lv_obj_t* obj) { lv_arc_set_end_angle(obj, pieEnd); });
      }

      int val = lastValue;
      if (lastStartValue && timerData.showElapsed &&
          (int)lastStartValue != lastValue)
        val = (int)lastStartValue - lastValue;

      if (isLarge) {
        char sDigitGroup0[LEN_TIMER_STRING];
        char sDigitGroup1[LEN_TIMER_STRING];
        char sUnit0[] = "M";
        char sUnit1[] = "S";

        splitTimer(sDigitGroup0, sDigitGroup1, sUnit0, sUnit1, abs(val), false);

        digits0.with(
            [&](lv_obj_t* obj) { lv_label_set_text(obj, sDigitGroup0); });
        digits1.with(
            [&](lv_obj_t* obj) { lv_label_set_text(obj, sDigitGroup1); });
        unit0.with([&](lv_obj_t* obj) { lv_label_set_text(obj, sUnit0); });
        unit1.with([&](lv_obj_t* obj) { lv_label_set_text(obj, sUnit1); });

        if (lastValue > 0 && lastStartValue > 0) {
          timerArc.with(
              [](lv_obj_t* obj) { lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN); });
          timerIcon->hide();
        } else {
          timerArc.with(
              [](lv_obj_t* obj) { lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN); });
          timerIcon->show();
        }
      } else {
        char str[LEN_TIMER_STRING];

        TimerOptions timerOptions;
        timerOptions.options = (abs(val) >= 3600) ? SHOW_TIME : SHOW_TIMER;
        getTimerString(str, abs(val), timerOptions);
        valLabel.with([&](lv_obj_t* obj) { lv_label_set_text(obj, str); });

        valLabel.with([&](lv_obj_t* obj) {
          if (width() <= SMALL_TXT_MAX_W && height() <= SMALL_TXT_MAX_H &&
              abs(val) >= 3600)
            lv_obj_add_state(obj, ETX_VALUE_SMALL_FONT);
          else
            lv_obj_clear_state(obj, ETX_VALUE_SMALL_FONT);
        });

        timerArc.with(
            [](lv_obj_t* obj) { lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN); });
        timerIcon->hide();
      }

      // Set colors if timer has elapsed.
      if (lastValue < 0 && lastValue % 2) {
        if (isLarge) {
          nameLabel.with(
              [](lv_obj_t* obj) { lv_obj_add_state(obj, ETX_NAME_TXT_WARNING); });
          digits0.with(
              [](lv_obj_t* obj) { lv_obj_add_state(obj, ETX_NAME_TXT_WARNING); });
          digits1.with(
              [](lv_obj_t* obj) { lv_obj_add_state(obj, ETX_NAME_TXT_WARNING); });
          unit0.with(
              [](lv_obj_t* obj) { lv_obj_add_state(obj, ETX_NAME_TXT_WARNING); });
          unit1.with(
              [](lv_obj_t* obj) { lv_obj_add_state(obj, ETX_NAME_TXT_WARNING); });
          clearState(ETX_STATE_BG_WARNING);
          timerBg->setColor(COLOR_THEME_WARNING_INDEX);
          timerIcon->setColor(COLOR_THEME_SECONDARY2_INDEX);
        } else {
          addState(ETX_STATE_BG_WARNING);
        }
      } else {
        if (isLarge) {
          nameLabel.with([](lv_obj_t* obj) {
            lv_obj_clear_state(obj, ETX_NAME_TXT_WARNING);
          });
          digits0.with([](lv_obj_t* obj) {
            lv_obj_clear_state(obj, ETX_NAME_TXT_WARNING);
          });
          digits1.with([](lv_obj_t* obj) {
            lv_obj_clear_state(obj, ETX_NAME_TXT_WARNING);
          });
          unit0.with([](lv_obj_t* obj) {
            lv_obj_clear_state(obj, ETX_NAME_TXT_WARNING);
          });
          unit1.with([](lv_obj_t* obj) {
            lv_obj_clear_state(obj, ETX_NAME_TXT_WARNING);
          });
          timerBg->setColor(COLOR_THEME_PRIMARY2_INDEX);
          timerIcon->setColor(COLOR_THEME_SECONDARY1_INDEX);
        }
        clearState(ETX_STATE_BG_WARNING);
      }
    }
  }

  static const WidgetOption options[];

  static LAYOUT_VAL_SCALED(SMALL_TXT_MAX_W, 100)
  static LAYOUT_VAL_SCALED(SMALL_TXT_MAX_H, 40)
  static LAYOUT_VAL_SCALED(VAL_LBL_Y, 20)
  static LAYOUT_VAL_SCALED(COMPACT_VAL_LBL_Y, 14)
  static LAYOUT_VAL_SCALED(TMR_LRG_W, 180)
  static LAYOUT_VAL_SCALED(TMR_LRG_H, 70)
  static LAYOUT_VAL_SCALED(TMR_ARC_SZ, 64)
  static LAYOUT_VAL_SCALED(TMR_ARC_W, 10)
  static LAYOUT_VAL_SCALED(NM_LRG_X, 78)
  static LAYOUT_VAL_SCALED(NM_LRG_Y, 19)
  static LAYOUT_VAL_SCALED(NM_LRG_W, 93)
  static LAYOUT_VAL_SCALED(U0_X, 111)
  static LAYOUT_VAL_SCALED(U0_Y, 33)
  static LAYOUT_VAL_SCALED(U1_X, 162)
  static LAYOUT_VAL_SCALED(U1_Y, 33)
  static LAYOUT_VAL_SCALED(D0_X, 74)
  static LAYOUT_VAL_SCALED(D0_Y, 31)
  static LAYOUT_VAL_SCALED(D1_X, 125)
  static LAYOUT_VAL_SCALED(D1_Y, 31)

 protected:
  tmrval_t lastValue = 0;
  uint32_t lastStartValue = -1;
  bool isLarge = false;
  lv_style_t style;
  RequiredLvObj nameLabel;
  RequiredLvObj valLabel;
  RequiredLvObj digits0;
  RequiredLvObj digits1;
  RequiredLvObj unit0;
  RequiredLvObj unit1;
  RequiredLvObj timerArc;
  StaticIcon* timerBg = nullptr;
  StaticIcon* timerIcon = nullptr;

  void onUpdate() override
  {
    auto widgetData = getPersistentData();

    // Set up widget from options.
    char s[16];

    uint32_t index = widgetData->options[0].value.unsignedValue;
    TimerData& timerData = g_model.timers[index];

    bool hasName = ZLEN(timerData.name) > 0;
    bool compact = isCompactTopBarWidget();

    if (width() >= TMR_LRG_W && height() >= TMR_LRG_H) {
      isLarge = true;
      nameLabel.with([&](lv_obj_t* obj) {
        etx_font(obj, FONT_XS_INDEX);
        lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        if (hasName)
          lv_obj_clear_state(obj, EXT_NAME_ALIGN_RIGHT);
        else
          lv_obj_add_state(obj, EXT_NAME_ALIGN_RIGHT);
        lv_obj_set_pos(obj, NM_LRG_X, NM_LRG_Y);
        lv_obj_set_width(obj, NM_LRG_W);
        lv_obj_clear_state(obj, ETX_NAME_COLOR_WHITE);
      });
      valLabel.with([](lv_obj_t* obj) {
        etx_font(obj, FONT_STD_INDEX);
        lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
      });
      digits0.with(
          [](lv_obj_t* obj) { lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN); });
      digits1.with(
          [](lv_obj_t* obj) { lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN); });
      unit0.with(
          [](lv_obj_t* obj) { lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN); });
      unit1.with(
          [](lv_obj_t* obj) { lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN); });
      timerBg->show();
    } else {
      isLarge = false;
      coord_t labelPad = PAD_TINY;
      coord_t labelWidth = width() > 2 * labelPad ? width() - 2 * labelPad : width();
      nameLabel.with([&](lv_obj_t* obj) {
        etx_font(obj, compact ? FONT_XXS_INDEX : FONT_XS_INDEX);
        lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_clear_state(obj, EXT_NAME_ALIGN_RIGHT);
        lv_obj_set_pos(obj, labelPad, 0);
        lv_obj_set_width(obj, labelWidth);
        lv_obj_add_state(obj, ETX_NAME_COLOR_WHITE);
      });
      valLabel.with([&](lv_obj_t* obj) {
        etx_font(obj, compact ? FONT_BOLD_INDEX : FONT_STD_INDEX);
        lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_width(obj, labelWidth);
        lv_obj_set_pos(obj, compact ? labelPad : PAD_THREE,
                       compact ? COMPACT_VAL_LBL_Y : VAL_LBL_Y);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
      });
      digits0.with(
          [](lv_obj_t* obj) { lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN); });
      digits1.with(
          [](lv_obj_t* obj) { lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN); });
      unit0.with(
          [](lv_obj_t* obj) { lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN); });
      unit1.with(
          [](lv_obj_t* obj) { lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN); });
      timerBg->hide();
    }

    // name
    if (hasName) {
      strAppend(s, timerData.name, LEN_TIMER_NAME);
    } else {  // user name not exist "TMRn"
      formatNumberAsString(s, 16, index + 1, 1, 0, "TMR");
    }
    nameLabel.with([&](lv_obj_t* obj) { lv_label_set_text(obj, s); });

    lastValue = 0;
    lastStartValue = -1;
    requireRefresh();
  }

  bool createUnitLabel(RequiredLvObj& label)
  {
    return initRequiredLvObj(
        label, [](lv_obj_t* parent) { return etx_label_create(parent); },
        [&](lv_obj_t* obj) {
          lv_label_set_text(obj, "");
          lv_obj_add_style(obj, &style, LV_PART_MAIN);
          etx_txt_color(obj, COLOR_THEME_SECONDARY1_INDEX);
          etx_txt_color(obj, COLOR_THEME_SECONDARY2_INDEX,
                        LV_PART_MAIN | ETX_NAME_TXT_WARNING);
        });
  }

  bool createDigitsLabel(RequiredLvObj& label)
  {
    if (!createUnitLabel(label)) return false;
    label.with([](lv_obj_t* obj) { etx_font(obj, FONT_XL_INDEX); });
    return true;
  }
};

const WidgetOption TimerWidget::options[] = {
    {STR_TIMER_SOURCE, WidgetOption::Timer, 0},
    {nullptr, WidgetOption::Bool}};

BaseWidgetFactory<TimerWidget> timerWidget("Timer", TimerWidget::options,
                                           STR_WIDGET_TIMER);
