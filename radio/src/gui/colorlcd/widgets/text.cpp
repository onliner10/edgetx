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

#define TEXT_WIDGET_DEFAULT_LABEL "My Label"

class TextWidget : public Widget
{
 public:
  TextWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
             WidgetLocation location) :
      Widget(factory, parent, rect, location)
  {
    delayWidgetLoad();
  }

  void delayedInit() override
  {
    lv_style_init(&style);

    initRequiredLvObj(
        shadow, [](lv_obj_t* parent) { return etx_label_create(parent); },
        [&](lv_obj_t* obj) {
          lv_obj_add_style(obj, &style, LV_PART_MAIN);
          lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_MAIN);
          lv_obj_set_pos(obj, 1, 1);
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
        });

    initRequiredLvObj(
        label, [](lv_obj_t* parent) { return etx_label_create(parent); },
        [&](lv_obj_t* obj) {
          lv_obj_add_style(obj, &style, LV_PART_MAIN);
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
        });

    update();
  }

  static const WidgetOption options[];

 protected:
  lv_style_t style;
  RequiredLvObj shadow;
  RequiredLvObj label;

  void onUpdate() override
  {
    auto widgetData = getPersistentData();

    // Set text value from options
    shadow.with([&](lv_obj_t* obj) {
      lv_label_set_text(obj, widgetData->options[0].value.stringValue.c_str());
    });
    label.with([&](lv_obj_t* obj) {
      lv_label_set_text(obj, widgetData->options[0].value.stringValue.c_str());
    });

    auto color = widgetData->options[1].value.unsignedValue;
    label.with([&](lv_obj_t* obj) {
      if (isCompactTopBarWidget() &&
          color == COLOR2FLAGS(COLOR_THEME_SECONDARY1_INDEX)) {
        etx_txt_color(obj, COLOR_THEME_PRIMARY2_INDEX);
      } else {
        etx_txt_color_from_flags(obj, color);
      }
    });

    FontIndex font = responsiveTextFont(height());
    shadow.with([&](lv_obj_t* obj) {
      layoutTextLabel(obj, {0, 0, width(), height()}, font, 1, 1);
    });
    label.with([&](lv_obj_t* obj) {
      layoutTextLabel(obj, {0, 0, width(), height()}, font);
    });

    // Show or hide shadow
    shadow.with([&](lv_obj_t* obj) {
      if (isMainViewWidget() && widgetData->options[3].value.boolValue)
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    });
  }
};

const WidgetOption TextWidget::options[] = {
    {STR_TEXT, WidgetOption::String,
     WIDGET_OPTION_VALUE_STRING(TEXT_WIDGET_DEFAULT_LABEL)},
    {STR_COLOR, WidgetOption::Color, COLOR2FLAGS(COLOR_THEME_SECONDARY1_INDEX)},
    {"", WidgetOption::TextSize, 0},
    {STR_SHADOW, WidgetOption::Bool, false},
    {"", WidgetOption::Align, ALIGN_LEFT},
    {nullptr, WidgetOption::Bool}};

BaseWidgetFactory<TextWidget> textWidget("Text", TextWidget::options,
                                         STR_WIDGET_TEXT);
