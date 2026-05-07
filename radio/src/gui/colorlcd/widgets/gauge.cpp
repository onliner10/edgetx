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

#include "edgetx.h"
#include "static.h"

class GaugeWidget : public TrackedWidget
{
 public:
  GaugeWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
              WidgetLocation location) :
      TrackedWidget(factory, parent, rect, location, LoadMode::Delayed)
  {
  }

  void delayedInit() override
  {
    // Gauge label
    if (!initRequiredWindow(sourceText, this, rect_t{0, 0, LV_SIZE_CONTENT, 16},
                            "", COLOR_THEME_PRIMARY2_INDEX, FONT(XS)))
      return;
    sourceText.withLive([](LiveWindow& live) {
      lv_label_set_long_mode(live.lvobj(), LV_LABEL_LONG_DOT);
    });

    if (!initRequiredWindow(
            valueText, this, rect_t{0, 0, lv_pct(100), GUAGE_H},
            [=]() { return getGuageValue(); }, COLOR_THEME_PRIMARY2_INDEX,
            FONT(XS) | CENTERED, "", "%"))
      return;
    valueText.withLive([](LiveWindow& live) {
      lv_label_set_long_mode(live.lvobj(), LV_LABEL_LONG_DOT);
      etx_obj_add_style(live.lvobj(), styles->text_align_right,
                        LV_STATE_USER_1);
    });

    if (!withLive([&](LiveWindow& live) {
          auto box = lv_obj_create(live.lvobj());
          if (!requireLvObj(box)) return false;
          lv_obj_set_pos(box, 0, GUAGE_H);
          lv_obj_set_size(box, lv_pct(100), GUAGE_H);
          lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
          etx_solid_bg(box, COLOR_THEME_PRIMARY2_INDEX);

          auto obj = lv_obj_create(box);
          if (!requireLvObj(bar, obj)) return false;
          lv_obj_set_pos(obj, 0, 0);
          lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
          etx_obj_add_style(obj, styles->bg_opacity_cover, LV_PART_MAIN);
          return true;
        }))
      return;

    update();
  }

  int16_t getGuageValue()
  {
    auto widgetData = getPersistentData();
    if (!widgetData) return 0;

    mixsrc_t index = widgetData->options[0].value.unsignedValue;
    int32_t min = widgetData->options[1].value.signedValue;
    int32_t max = widgetData->options[2].value.signedValue;
    if (min == max) return 0;

    int32_t value = getValue(index);

    if (min > max) {
      SWAP(min, max);
      value = value - min - max;
    }

    value = limit(min, value, max);

    return divRoundClosest(100 * (value - min), (max - min));
  }

  void onUpdate() override
  {
    auto widgetData = getPersistentData();
    if (!widgetData) return;

    mixsrc_t index = widgetData->options[0].value.unsignedValue;
    bool compact = isCompactTopBarWidget();
    sourceText.with([&](StaticText& text) {
      text.setText(getSourceString(index));
      if (compact) {
        text.setRect({0, 0, width(), GUAGE_H});
        text.font(FONT_XXS_INDEX);
      } else {
        text.setRect({0, 0, LV_SIZE_CONTENT, GUAGE_H});
        text.font(FONT_XS_INDEX);
      }
    });

    valueText.with([&](DynamicNumber<int16_t>& text) {
      text.font(FONT_XS_INDEX);
      if (compact || width() < ALIGN_MAX_W)
        text.addState(LV_STATE_USER_1);
      else
        text.clearState(LV_STATE_USER_1);
    });

    bar.with([&](lv_obj_t* obj) {
      etx_bg_color_from_flags(obj, widgetData->options[3].value.unsignedValue);
    });
    lastValue = -10000;
    requireRefresh();
  }

  static const WidgetOption options[];

 protected:
  int16_t lastValue = -10000;
  RequiredWindow<StaticText> sourceText;
  RequiredWindow<DynamicNumber<int16_t>> valueText;
  RequiredLvObj bar;

  uint32_t refreshKey() override
  {
    WidgetRefreshKey key;
    key.add((int32_t)getGuageValue());
    return key.value();
  }

  void refresh() override
  {
    auto newValue = getGuageValue();
    if (lastValue != newValue) {
      lastValue = newValue;

      lv_coord_t w = (width() * lastValue) / 100;
      bar.with([&](lv_obj_t* obj) { lv_obj_set_size(obj, w, GUAGE_H); });
    }
  }

  static LAYOUT_VAL_SCALED(GUAGE_H, 16)
  static LAYOUT_VAL_SCALED(ALIGN_MAX_W, 90)
};

const WidgetOption GaugeWidget::options[] = {
    {STR_SOURCE, WidgetOption::Source, 1},
    {STR_MIN, WidgetOption::Integer, WIDGET_OPTION_VALUE_SIGNED(-RESX),
     WIDGET_OPTION_VALUE_SIGNED(-RESX), WIDGET_OPTION_VALUE_SIGNED(RESX)},
    {STR_MAX, WidgetOption::Integer, WIDGET_OPTION_VALUE_SIGNED(RESX),
     WIDGET_OPTION_VALUE_SIGNED(-RESX), WIDGET_OPTION_VALUE_SIGNED(RESX)},
    {STR_COLOR, WidgetOption::Color, COLOR2FLAGS(COLOR_THEME_WARNING_INDEX)},
    {nullptr, WidgetOption::Bool}};

BaseWidgetFactory<GaugeWidget> gaugeWidget("Gauge", GaugeWidget::options,
                                           STR_WIDGET_GAUGE);
