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
    delayLoad();
  }

  void delayedInit() override
  {
    lv_style_init(&style);

    shadow = etx_label_create(lvobj);
    lv_obj_add_style(shadow, &style, LV_PART_MAIN);
    lv_obj_set_style_text_color(shadow, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_pos(shadow, 1, 1);
    lv_label_set_long_mode(shadow, LV_LABEL_LONG_DOT);

    label = etx_label_create(lvobj);
    lv_obj_add_style(label, &style, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

    update();
  }

  static const WidgetOption options[];

 protected:
  lv_style_t style;
  lv_obj_t* shadow;
  lv_obj_t* label;

  void update() override
  {
    if (!loaded || _deleted) return;

    auto widgetData = getPersistentData();

    // Set text value from options
    lv_label_set_text(shadow, widgetData->options[0].value.stringValue.c_str());
    lv_label_set_text(label, widgetData->options[0].value.stringValue.c_str());

    auto color = widgetData->options[1].value.unsignedValue;
    if (isCompactTopBarWidget() &&
        color == COLOR2FLAGS(COLOR_THEME_SECONDARY1_INDEX)) {
      etx_txt_color(label, COLOR_THEME_PRIMARY2_INDEX);
    } else {
      etx_txt_color_from_flags(label, color);
    }

    FontIndex font = responsiveTextFont(height());
    layoutTextLabel(shadow, {0, 0, width(), height()}, font, 1, 1);
    layoutTextLabel(label, {0, 0, width(), height()}, font);

    // Show or hide shadow
    if (isMainViewWidget() && widgetData->options[3].value.boolValue)
      lv_obj_clear_flag(shadow, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(shadow, LV_OBJ_FLAG_HIDDEN);
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
