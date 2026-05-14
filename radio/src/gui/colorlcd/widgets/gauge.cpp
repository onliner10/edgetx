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
#include "static.h"
#include "widget.h"

class GaugeWidget : public NativeWidget
{
 public:
  GaugeWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
              WidgetLocation location) :
      NativeWidget(factory, parent, rect, location)
  {
  }

  void createContent(lv_obj_t* parent) override
  {
    (void)parent;

    initRequiredLvObj(
        contentBox,
        [](lv_obj_t* parent) {
          return createFlexBox(parent, LV_FLEX_FLOW_COLUMN);
        },
        [](lv_obj_t*) {});
    initRequiredLvObj(
        headerBox,
        [&](lv_obj_t* parent) {
          contentBox.with([&](lv_obj_t* obj) { parent = obj; });
          return createFlexBox(parent, LV_FLEX_FLOW_ROW);
        },
        [](lv_obj_t*) {});

    // Gauge label
    if (!initRequiredWindow(sourceText, this, rect_t{0, 0, LV_SIZE_CONTENT, 16},
                            "", COLOR_THEME_PRIMARY2_INDEX, FONT(XS)))
      return;
    sourceText.withLive([](LiveWindow& live) {
      lv_label_set_long_mode(live.lvobj(), LV_LABEL_LONG_DOT);
    });
    sourceText.withLive([&](LiveWindow& live) {
      headerBox.with(
          [&](lv_obj_t* obj) { lv_obj_set_parent(live.lvobj(), obj); });
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
    valueText.withLive([&](LiveWindow& live) {
      headerBox.with(
          [&](lv_obj_t* obj) { lv_obj_set_parent(live.lvobj(), obj); });
    });

    if (!withLive([&](LiveWindow& live) {
          lv_obj_t* parent = live.lvobj();
          contentBox.with([&](lv_obj_t* obj) { parent = obj; });
          auto box = lv_obj_create(parent);
          if (!requireLvObj(track, box)) return false;
          lv_obj_remove_style_all(box);
          lv_obj_clear_flag(box,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
          lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
          lv_obj_set_style_bg_color(box, trackColor(), LV_PART_MAIN);
          lv_obj_set_style_radius(box, PILL_RADIUS, LV_PART_MAIN);

          auto obj = lv_obj_create(box);
          if (!requireLvObj(bar, obj)) return false;
          lv_obj_remove_style_all(obj);
          lv_obj_set_pos(obj, 0, 0);
          lv_obj_clear_flag(obj,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
          lv_obj_set_style_radius(obj, PILL_RADIUS, LV_PART_MAIN);
          etx_obj_add_style(obj, styles->bg_opacity_cover, LV_PART_MAIN);

          obj = etx_label_create(box, FONT_XS_INDEX);
          if (!requireLvObj(valueOverlay, obj)) return false;
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_label_set_text(obj, "");
          lv_obj_set_style_text_color(obj, primaryTextColor(), LV_PART_MAIN);
          lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
          return true;
        }))
      return;
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

  void layoutContent(const rect_t& content) override
  {
    auto widgetData = getPersistentData();
    if (!widgetData) return;

    mixsrc_t index = widgetData->options[0].value.unsignedValue;
    bool compact = isCompactTopBarWidget();
    bool stackCard = usesCardChrome() && content.h >= 72;
    const char* source = getSourceString(index);
    coord_t gap = usesCardChrome() ? cardGap(content) : PAD_TINY;
    coord_t titleH = 0;
    coord_t barH = usesCardChrome() ? cardBarHeight(content)
                                    : (content.h < 58 ? 10 : GUAGE_H);
    coord_t headerH = usesCardChrome() ? cardHeaderHeight(content) : GUAGE_H;
    if (stackCard) {
      FontIndex titleFont = cardStackTitleFont(content);
      titleH = getFontHeight(LcdFlags(titleFont) << 8u);
      barH = content.h > titleH + gap ? content.h - titleH - gap : barH;
      headerH = titleH;
    }

    sourceText.with([&](StaticText& text) {
      text.setText(source);
      if (stackCard) {
        text.font(cardStackTitleFont(content));
        text.setRect({0, 0, content.w, titleH});
      } else {
        text.font(usesCardChrome() ? cardTitleFont(content)
                                   : (compact ? FONT_XXS_INDEX
                                              : FONT_XS_INDEX));
      }
      if (usesCardChrome()) {
        text.withLive([&](LiveWindow& live) {
          lv_obj_set_style_text_color(live.lvobj(), mutedTextColor(),
                                      LV_PART_MAIN);
          lv_obj_set_style_text_align(live.lvobj(), LV_TEXT_ALIGN_LEFT,
                                      LV_PART_MAIN);
        });
      }
    });

    valueText.with([&](DynamicNumber<int16_t>& text) {
      text.font(usesCardChrome() ? cardValueFont(content)
                                 : (content.h < 58 ? FONT_XXS_INDEX
                                                   : FONT_XS_INDEX));
      if (compact || width() < ALIGN_MAX_W || usesCardChrome())
        text.addState(LV_STATE_USER_1);
      else
        text.clearState(LV_STATE_USER_1);
      if (usesCardChrome()) {
        text.withLive([&](LiveWindow& live) {
          lv_obj_set_style_text_color(live.lvobj(), primaryTextColor(),
                                      LV_PART_MAIN);
          setObjVisible(live.lvobj(), !stackCard);
        });
      }
    });

    valueOverlay.with([&](lv_obj_t* obj) {
      setObjVisible(obj, stackCard);
      if (!stackCard) return;
      static constexpr FontIndex valueFonts[] = {
          FONT_XXL_INDEX, FONT_LXL_INDEX, FONT_XL_INDEX,   FONT_L_INDEX,
          FONT_BOLD_INDEX, FONT_STD_INDEX, FONT_XS_INDEX, FONT_XXS_INDEX};
      FontIndex valueFont = fitTextFont("100%", content.w, barH, valueFonts,
                                        DIM(valueFonts));
      coord_t valueH = getFontHeight(LcdFlags(valueFont) << 8u);
      coord_t valueY = barH > valueH ? (barH - valueH) / 2 : 0;
      etx_font(obj, valueFont);
      lv_obj_set_style_text_color(obj, primaryTextColor(), LV_PART_MAIN);
      lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      setObjRect(obj, 0, valueY, content.w, valueH);
      lv_obj_move_foreground(obj);
    });

    contentBox.with([&](lv_obj_t* obj) {
      layoutFlexBox(obj, content, LV_FLEX_FLOW_COLUMN, gap,
                    LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    });
    headerBox.with([&](lv_obj_t* obj) {
      setFlexChild(obj, content.w, headerH);
      sourceText.withLive([&](LiveWindow& title) {
        valueText.withLive([&](LiveWindow& value) {
          lv_obj_set_parent(title.lvobj(), obj);
          if (!stackCard) {
            lv_obj_set_parent(value.lvobj(), obj);
            layoutCardHeader(obj, title.lvobj(), value.lvobj(),
                             {0, 0, content.w, headerH});
          }
        });
      });
    });
    track.with([&](lv_obj_t* obj) {
      setFlexChild(obj, content.w, barH);
      if (stackCard) {
        valueOverlay.with(
            [](lv_obj_t* value) { lv_obj_move_foreground(value); });
      }
    });
    bar.with([&](lv_obj_t* obj) {
      etx_bg_color_from_flags(obj, widgetData->options[3].value.unsignedValue);
      if (stackCard) lv_obj_move_background(obj);
    });
    lastValue = -10000;
    invalidateNativeRefresh();
  }

  static const WidgetOption options[];

 protected:
  int16_t lastValue = -10000;
  RequiredWindow<StaticText> sourceText;
  RequiredWindow<DynamicNumber<int16_t>> valueText;
  RequiredLvObj contentBox;
  RequiredLvObj headerBox;
  RequiredLvObj track;
  RequiredLvObj bar;
  RequiredLvObj valueOverlay;

  uint32_t contentRefreshKey() override
  {
    WidgetRefreshKey key;
    key.add((int32_t)getGuageValue());
    return key.value();
  }

  void refreshContent() override
  {
    auto newValue = getGuageValue();
    if (lastValue != newValue) {
      lastValue = newValue;

      lv_coord_t w = 1;
      track.with([&](lv_obj_t* obj) { w = lv_obj_get_width(obj); });
      w = (w * lastValue) / 100;
      coord_t h = GUAGE_H;
      track.with([&](lv_obj_t* obj) { h = lv_obj_get_height(obj); });
      bar.with([&](lv_obj_t* obj) {
        lv_obj_set_size(obj, w, h);
        lv_obj_move_background(obj);
      });
      std::string value = formatNumberAsString(lastValue, 0, 0, "", "%");
      valueOverlay.with([&](lv_obj_t* obj) {
        lv_label_set_text(obj, value.c_str());
        lv_obj_move_foreground(obj);
      });
    }
  }

  static LAYOUT_VAL_SCALED(GUAGE_H, 16) static LAYOUT_VAL_SCALED(ALIGN_MAX_W,
                                                                 90)
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
