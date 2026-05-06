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

#include "edgetx.h"
#include "widget.h"

#define ETX_STATE_TIMER_ELAPSED LV_STATE_USER_1
#define ETX_STATE_TELEM_STALE LV_STATE_USER_2
#define ETX_STATE_LARGE_FONT LV_STATE_USER_3

class ValueWidget : public TrackedWidget
{
 public:
  ValueWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
              WidgetLocation location) :
      TrackedWidget(factory, parent, rect, location, LoadMode::Delayed)
  {
  }

  void delayedInit() override
  {
    lv_style_init(&labelStyle);
    lv_style_set_width(&labelStyle, lv_pct(100));
    lv_style_set_height(&labelStyle, lv_pct(100));

    lv_style_init(&valueStyle);
    lv_style_set_width(&valueStyle, lv_pct(100));
    lv_style_set_height(&valueStyle, lv_pct(100));

    initRequiredLvObj(
        labelShadow, [](lv_obj_t* parent) { return etx_label_create(parent); },
        [&](lv_obj_t* obj) {
          lv_obj_add_style(obj, &labelStyle, LV_PART_MAIN);
          lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN);
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_label_set_text(obj, "");
        });

    initRequiredLvObj(
        label, [](lv_obj_t* parent) { return etx_label_create(parent); },
        [&](lv_obj_t* obj) {
          lv_obj_add_style(obj, &labelStyle, LV_PART_MAIN);
          etx_txt_color(obj, COLOR_THEME_WARNING_INDEX,
                        ETX_STATE_TIMER_ELAPSED);
          etx_txt_color(obj, COLOR_THEME_DISABLED_INDEX, ETX_STATE_TELEM_STALE);
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_label_set_text(obj, "");
        });

    initRequiredLvObj(
        valueShadow,
        [](lv_obj_t* parent) { return etx_label_create(parent, FONT_L_INDEX); },
        [&](lv_obj_t* obj) {
          lv_obj_add_style(obj, &valueStyle, LV_PART_MAIN);
          lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN);
          etx_font(obj, FONT_XL_INDEX, ETX_STATE_LARGE_FONT);
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_label_set_text(obj, "");
        });

    initRequiredLvObj(
        value,
        [](lv_obj_t* parent) { return etx_label_create(parent, FONT_L_INDEX); },
        [&](lv_obj_t* obj) {
          lv_obj_add_style(obj, &valueStyle, LV_PART_MAIN);
          etx_txt_color(obj, COLOR_THEME_WARNING_INDEX,
                        ETX_STATE_TIMER_ELAPSED);
          etx_txt_color(obj, COLOR_THEME_DISABLED_INDEX, ETX_STATE_TELEM_STALE);
          etx_font(obj, FONT_XL_INDEX, ETX_STATE_LARGE_FONT);
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_label_set_text(obj, "");
        });

    update();
    foreground();
  }

  uint32_t refreshKey() override
  {
    auto widgetData = getPersistentData();
    mixsrc_t field = widgetData->options[0].value.unsignedValue;

    WidgetRefreshKey key;
    key.add((int32_t)field).add((int32_t)getValue(field));

    if (field >= MIXSRC_FIRST_TELEM) {
      TelemetryItem& telemetryItem =
          telemetryItems[(field - MIXSRC_FIRST_TELEM) / 3];
      key.add(telemetryItem.isAvailable()).add(telemetryItem.isOld());
    } else if (field >= MIXSRC_FIRST_TIMER && field <= MIXSRC_LAST_TIMER) {
      TimerState& timerState = timersStates[field - MIXSRC_FIRST_TIMER];
      key.add((int32_t)timerState.val);
    }

    return key.value();
  }

  void refresh() override
  {
    bool changed = false;

    auto widgetData = getPersistentData();

    // get source from options[0]
    mixsrc_t field = widgetData->options[0].value.unsignedValue;

    // if value changed
    auto newValue = getValue(field);
    if (lastValue != newValue) {
      lastValue = newValue;
      changed = true;
    } else {
      // if telemetry value, and telemetry offline or old data
      if (field >= MIXSRC_FIRST_TELEM) {
        TelemetryItem& telemetryItem =
            telemetryItems[(field - MIXSRC_FIRST_TELEM) / 3];
        bool telemState = !telemetryItem.isAvailable() || telemetryItem.isOld();
        if (lastTelemState != telemState) {
          lastTelemState = telemState;
          changed = true;
        }
      }
    }

    if (changed) {
      // Set color to option value
      label.with([](lv_obj_t* obj) {
        lv_obj_clear_state(obj, ETX_STATE_TIMER_ELAPSED | ETX_STATE_TELEM_STALE);
      });
      value.with([](lv_obj_t* obj) {
        lv_obj_clear_state(obj, ETX_STATE_TIMER_ELAPSED | ETX_STATE_TELEM_STALE);
      });

      // Check for disabled or warning color states
      if (field >= MIXSRC_FIRST_TIMER && field <= MIXSRC_LAST_TIMER) {
        TimerState& timerState = timersStates[field - MIXSRC_FIRST_TIMER];
        if (timerState.val < 0) {
          // Set warning color
          label.with([](lv_obj_t* obj) {
            lv_obj_add_state(obj, ETX_STATE_TIMER_ELAPSED);
          });
          value.with([](lv_obj_t* obj) {
            lv_obj_add_state(obj, ETX_STATE_TIMER_ELAPSED);
          });
        }
      } else if (field >= MIXSRC_FIRST_TELEM) {
        TelemetryItem& telemetryItem =
            telemetryItems[(field - MIXSRC_FIRST_TELEM) / 3];
        if (!telemetryItem.isAvailable() || telemetryItem.isOld()) {
          // Set disabled color
          label.with(
              [](lv_obj_t* obj) { lv_obj_add_state(obj, ETX_STATE_TELEM_STALE); });
          value.with(
              [](lv_obj_t* obj) { lv_obj_add_state(obj, ETX_STATE_TELEM_STALE); });
        }
      }

      std::string valueTxt;

      // Set value text
      if (field == MIXSRC_TX_VOLTAGE) {
        valueTxt =
            getSourceCustomValueString(field, getValue(field), valueFlags);
        valueTxt += STR_V;
      } else if (field == MIXSRC_TX_TIME) {
        int32_t tme = getValue(MIXSRC_TX_TIME);
        TimerOptions timerOptions;
        timerOptions.options = SHOW_TIME;
        valueTxt = getTimerString(tme, timerOptions);
      } else if (field >= MIXSRC_FIRST_TIMER && field <= MIXSRC_LAST_TIMER) {
        TimerState& timerState = timersStates[field - MIXSRC_FIRST_TIMER];
        TimerOptions timerOptions;
        timerOptions.options = SHOW_TIMER;
        valueTxt = getTimerString(abs(timerState.val), timerOptions);
      } else if (field >= MIXSRC_FIRST_TELEM) {
        std::string getSensorCustomValue(uint8_t sensor, int32_t value,
                                         LcdFlags flags);
        valueTxt = getSensorCustomValue((field - MIXSRC_FIRST_TELEM) / 3,
                                        getValue(field), valueFlags);
#if defined(LUA_INPUTS)
      }
      else if (field >= MIXSRC_FIRST_LUA && field <= MIXSRC_LAST_LUA) {
        valueTxt =
            getSourceCustomValueString(field, calcRESXto1000(getValue(field)), valueFlags | PREC1);
#endif
      } else {
        valueTxt =
            getSourceCustomValueString(field, getValue(field), valueFlags);
      }

      value.with(
          [&](lv_obj_t* obj) { lv_label_set_text(obj, valueTxt.c_str()); });
      valueShadow.with(
          [&](lv_obj_t* obj) { lv_label_set_text(obj, valueTxt.c_str()); });
    }
  }

  static const WidgetOption options[];

 protected:
  int32_t lastValue = -10000;
  bool lastTelemState = false;
  lv_style_t labelStyle;
  lv_style_t valueStyle;
  RequiredLvObj label;
  RequiredLvObj labelShadow;
  RequiredLvObj value;
  RequiredLvObj valueShadow;
  LcdFlags valueFlags = 0;

  static LAYOUT_VAL_SCALED(VAL_Y1, 14)
  static LAYOUT_VAL_SCALED(VAL_Y2, 18)
  static LAYOUT_VAL_SCALED(COMPACT_VAL_Y, 14)
  static LAYOUT_VAL_SCALED(H_CHK, 50)
  static LAYOUT_VAL_SCALED(W_CHK, 120)

  void onUpdate() override
  {
    auto widgetData = getPersistentData();

    // get source from options[0]
    mixsrc_t field = widgetData->options[0].value.unsignedValue;

    // get color from options[1]
    label.with([&](lv_obj_t* obj) {
      etx_txt_color_from_flags(obj, widgetData->options[1].value.unsignedValue);
    });
    value.with([&](lv_obj_t* obj) {
      etx_txt_color_from_flags(obj, widgetData->options[1].value.unsignedValue);
    });

    // get label alignment from options[3]
    LcdFlags lblAlign = widgetData->options[3].value.unsignedValue;

    // get value alignment from options[4]
    LcdFlags valAlign = widgetData->options[4].value.unsignedValue;

    lv_coord_t labelX = 0;
    lv_coord_t labelY = 0;
    lv_coord_t valueX = 0;
    lv_coord_t valueY = VAL_Y1;
    bool compact = isCompactTopBarWidget();

    label.with([](lv_obj_t* obj) { etx_font(obj, FONT_STD_INDEX); });
    labelShadow.with([](lv_obj_t* obj) { etx_font(obj, FONT_STD_INDEX); });
    value.with([](lv_obj_t* obj) {
      etx_font(obj, FONT_L_INDEX);
      lv_obj_clear_state(obj, ETX_STATE_LARGE_FONT);
    });
    valueShadow.with([](lv_obj_t* obj) {
      etx_font(obj, FONT_L_INDEX);
      lv_obj_clear_state(obj, ETX_STATE_LARGE_FONT);
    });

    // Get positions, alignment and value font size.
    if (compact) {
      lblAlign = ALIGN_LEFT;
      valAlign = ALIGN_LEFT;
      labelX = PAD_TINY;
      labelY = 0;
      valueX = PAD_TINY;
      valueY = COMPACT_VAL_Y;
      label.with([](lv_obj_t* obj) { etx_font(obj, FONT_XXS_INDEX); });
      labelShadow.with([](lv_obj_t* obj) { etx_font(obj, FONT_XXS_INDEX); });
      value.with([](lv_obj_t* obj) { etx_font(obj, FONT_BOLD_INDEX); });
      valueShadow.with([](lv_obj_t* obj) { etx_font(obj, FONT_BOLD_INDEX); });
    } else if (height() < H_CHK) {
      if (width() >= W_CHK) {
        lblAlign = ALIGN_LEFT;
        valAlign = ALIGN_RIGHT;
        labelX = PAD_SMALL;
        labelY = PAD_TINY;
        valueX = -PAD_SMALL;
        valueY = -PAD_TINY;
      }
    } else {
      labelX = (lblAlign == ALIGN_LEFT)     ? PAD_SMALL
               : (lblAlign == ALIGN_CENTER) ? -PAD_THREE
                                            : -PAD_SMALL;
      labelY = 2;
      valueX = (valAlign == ALIGN_LEFT)     ? PAD_SMALL
               : (valAlign == ALIGN_CENTER) ? 1
                                            : -PAD_SMALL;
      valueY = VAL_Y2;
      if (field >= MIXSRC_FIRST_TELEM) {
        int8_t sensor = 1 + (field - MIXSRC_FIRST_TELEM) / 3;
        if (!isGPSSensor(sensor) && !isSensorUnit(sensor, UNIT_DATETIME) && !isSensorUnit(sensor, UNIT_TEXT)) {
          // Set font to XL
          value.with(
              [](lv_obj_t* obj) { lv_obj_add_state(obj, ETX_STATE_LARGE_FONT); });
          valueShadow.with(
              [](lv_obj_t* obj) { lv_obj_add_state(obj, ETX_STATE_LARGE_FONT); });
        }
      }
#if defined(INTERNAL_GPS)
      else if (field == MIXSRC_TX_GPS) {
      }
#endif
      else {
        // Set font to XL
        value.with(
            [](lv_obj_t* obj) { lv_obj_add_state(obj, ETX_STATE_LARGE_FONT); });
        valueShadow.with(
            [](lv_obj_t* obj) { lv_obj_add_state(obj, ETX_STATE_LARGE_FONT); });
      }
    }

    // Set text alignment
    lv_style_set_text_align(&labelStyle,
                            (lblAlign == ALIGN_RIGHT)    ? LV_TEXT_ALIGN_RIGHT
                            : (lblAlign == ALIGN_CENTER) ? LV_TEXT_ALIGN_CENTER
                                                         : LV_TEXT_ALIGN_LEFT);
    lv_style_set_text_align(&valueStyle,
                            (valAlign == ALIGN_RIGHT)    ? LV_TEXT_ALIGN_RIGHT
                            : (valAlign == ALIGN_CENTER) ? LV_TEXT_ALIGN_CENTER
                                                         : LV_TEXT_ALIGN_LEFT);

    // Set label text
    char* labelTxt = getSourceString(field);
    label.with([&](lv_obj_t* obj) { lv_label_set_text(obj, labelTxt); });
    labelShadow.with([&](lv_obj_t* obj) { lv_label_set_text(obj, labelTxt); });

    // Set label and value positions.
    labelShadow.with([&](lv_obj_t* obj) {
      lv_obj_set_pos(obj, labelX + 1, labelY + 1);
    });
    label.with([&](lv_obj_t* obj) { lv_obj_set_pos(obj, labelX, labelY); });
    valueShadow.with([&](lv_obj_t* obj) {
      lv_obj_set_pos(obj, valueX + 1, valueY + 1);
    });
    value.with([&](lv_obj_t* obj) { lv_obj_set_pos(obj, valueX, valueY); });

    // Show / hide shadow
    labelShadow.with([&](lv_obj_t* obj) {
      if (widgetData->options[2].value.boolValue)
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    });
    valueShadow.with([&](lv_obj_t* obj) {
      if (widgetData->options[2].value.boolValue)
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    });

    lastValue = -10000;
    requireRefresh();
  }
};

const WidgetOption ValueWidget::options[] = {
    {STR_SOURCE, WidgetOption::Source, MIXSRC_FIRST_STICK},
    {STR_COLOR, WidgetOption::Color, COLOR2FLAGS(COLOR_THEME_PRIMARY2_INDEX)},
    {STR_SHADOW, WidgetOption::Bool, false},
    {STR_ALIGN_LABEL, WidgetOption::Align, ALIGN_LEFT},
    {STR_ALIGN_VALUE, WidgetOption::Align, ALIGN_LEFT},
    {nullptr, WidgetOption::Bool}};

BaseWidgetFactory<ValueWidget> ValueWidget("Value", ValueWidget::options,
                                           STR_WIDGET_VALUE);
