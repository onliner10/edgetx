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

#include <new>

#include "edgetx.h"
#include "messaging.h"
#include "widget.h"

constexpr int16_t OUTPUT_INVALID_VALUE = INT16_MIN;
constexpr coord_t OUTPUT_PILL_RADIUS = 0;

#define ETX_STATE_BG_FILL LV_STATE_USER_1

class ChannelValue : public Window
{
 public:
  ChannelValue(Widget* parent, const rect_t& rect, uint8_t channel,
               LcdFlags txtColor, LcdFlags barColor, coord_t rowHeight) :
      Window(parent, rect),
      channel(channel),
      txtColor(txtColor),
      barColor(barColor),
      rowHeight(rowHeight),
      compactTopBar(parent->isCompactTopBarWidget())
  {
    setWindowFlag(NO_FOCUS | NO_CLICK);

    refreshMsg.subscribe(Messaging::REFRESH_OUTPUTS_WIDGET,
                         [=](uint32_t param) { refresh(); });

    delayLoad();
  }

  void delayedInit() override
  {
    padAll(PAD_ZERO);

    if (!withLive([&](LiveWindow& live) {
          lv_obj_set_layout(live.lvobj(), LV_LAYOUT_FLEX);
          lv_obj_set_flex_flow(live.lvobj(), LV_FLEX_FLOW_COLUMN);
          lv_obj_set_flex_align(live.lvobj(), LV_FLEX_ALIGN_START,
                                LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
          lv_obj_set_style_pad_all(live.lvobj(), 0, LV_PART_MAIN);
          lv_obj_set_style_pad_row(live.lvobj(), 1, LV_PART_MAIN);
          lv_obj_set_style_pad_column(live.lvobj(), 0, LV_PART_MAIN);

          auto obj = lv_obj_create(live.lvobj());
          if (!requireLvObj(rowBox, obj)) return false;
          lv_obj_t* row = obj;
          lv_obj_remove_style_all(obj);
          lv_obj_clear_flag(obj,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
          lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
          lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
          lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                                LV_FLEX_ALIGN_START);
          lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
          lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
          lv_obj_set_style_pad_column(obj, PAD_TINY, LV_PART_MAIN);
          lv_obj_set_size(obj, width(), labelHeight());

          obj = lv_obj_create(live.lvobj());
          if (!requireLvObj(track, obj)) return false;
          lv_obj_t* trackObj = obj;
          lv_obj_remove_style_all(obj);
          lv_obj_clear_flag(obj,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
          lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
          lv_obj_set_style_bg_color(obj, lv_color_make(215, 228, 238),
                                    LV_PART_MAIN);
          lv_obj_set_style_radius(obj, OUTPUT_PILL_RADIUS, LV_PART_MAIN);
          lv_obj_set_size(obj, width(), barHeight());

          obj = lv_obj_create(trackObj);
          if (!requireLvObj(bar, obj)) return false;
          lv_obj_remove_style_all(obj);
          lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
          lv_obj_clear_flag(obj,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
          lv_obj_set_style_radius(obj, OUTPUT_PILL_RADIUS, LV_PART_MAIN);
          lv_obj_set_size(obj, 0, barHeight());
          etx_bg_color_from_flags(obj, barColor);

          obj = etx_label_create(row, labelFont());
          if (!requireLvObj(chanLabel, obj)) return false;
          etx_obj_add_style(obj, styles->text_align_left, LV_PART_MAIN);
          etx_txt_color_from_flags(obj, txtColor);
          lv_obj_set_size(obj, labelWidth(), labelHeight());
          lv_obj_set_flex_grow(obj, 1);
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_label_set_text(obj, "");

          obj = etx_label_create(row, labelFont());
          if (!requireLvObj(valueLabel, obj)) return false;
          etx_obj_add_style(obj, styles->text_align_right, LV_PART_MAIN);
          etx_txt_color_from_flags(obj, txtColor);
          lv_obj_set_size(obj, valueWidth(), labelHeight());
          if (showValue())
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
          else
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_label_set_text(obj, "");

          return true;
        }))
      return;

    chanHasName = g_model.limitData[channel].name[0] != 0;
    setChannel();

    refresh();
  }

  FontIndex labelFont() const
  {
    return labelFont(rowHeight);
  }

  bool showValue() const { return rowHeight > 16 && width() >= 92; }

  coord_t labelHeight() const
  {
    coord_t fontH = getFontHeight(LcdFlags(labelFont()) << 8u);
    coord_t h = rowHeight - barHeight() - 1;
    return h > fontH ? h : fontH;
  }

  coord_t valueWidth() const
  {
    if (!showValue()) return 1;
    coord_t w = width() / 3;
    return w > 28 ? w : 28;
  }

  coord_t labelWidth() const
  {
    if (!showValue()) return width();
    coord_t w = width() - valueWidth() - PAD_TINY;
    return w > 1 ? w : 1;
  }

  void setChannel()
  {
    char s[16];
    if (chanHasName) {
      formatNumberAsString(s, 16, channel + 1, LEADING0, 2, "", " ");
      strAppend(s + 3, g_model.limitData[channel].name, LEN_CHANNEL_NAME);
    } else {
      getSourceString(s, MIXSRC_FIRST_CH + channel);
    }
    chanLabel.with([&](lv_obj_t* obj) { lv_label_set_text(obj, s); });
  }

  coord_t barHeight() const
  {
    return barHeight(rowHeight);
  }

  static FontIndex labelFont(coord_t height)
  {
    return height <= 16 ? FONT_XXS_INDEX : FONT_XS_INDEX;
  }

  static coord_t barHeight(coord_t height)
  {
    coord_t h = height / 3;
    if (h < 5) h = 5;
    if (h > 10) h = 10;
    return h;
  }

  static coord_t rowHeightFor(bool compact)
  {
    coord_t h = compact ? COMPACT_ROW_HEIGHT : ROW_HEIGHT;
    for (uint8_t i = 0; i < 2; i += 1) {
      coord_t fontH = getFontHeight(LcdFlags(labelFont(h)) << 8u);
      coord_t needed = fontH + barHeight(h) + 1;
      if (h >= needed) break;
      h = needed;
    }
    return h;
  }

  void refresh()
  {
    runWhenLoaded([&]() {
      int16_t value = getChannelOutput(channel);

      if (value != lastValue) {
        lastValue = value;

        std::string s;
        if (g_eeGeneral.ppmunit == PPM_US)
          s = formatNumberAsString(PPM_CH_CENTER(channel) + value / 2, 0, 0, "",
                                   STR_US);
        else if (g_eeGeneral.ppmunit == PPM_PERCENT_PREC1)
          s = formatNumberAsString(calcRESXto1000(value), PREC1, 0, "", "%");
        else
          s = formatNumberAsString(calcRESXto100(value), 0, 0, "", "%");

        if (s != lastText) {
          lastText = s;
          valueLabel.with(
              [&](lv_obj_t* obj) { lv_label_set_text(obj, s.c_str()); });
        }

        const int lim =
            (g_model.extendedLimits ? (1024 * LIMIT_EXT_PERCENT / 100) : 1024);
        uint16_t w = width();
        int16_t scaledValue =
            divRoundClosest(w * limit<int16_t>(-lim, value, lim), lim * 2);

        if (scaledValue != lastScaledValue) {
          lastScaledValue = scaledValue;

          uint16_t fillW = abs(scaledValue);
          uint16_t x = value > 0 ? w / 2 : w / 2 - fillW + 1;
          coord_t bh = barHeight();
          track.with([&](lv_obj_t* obj) { lv_obj_set_size(obj, w, bh); });

          bar.with([&](lv_obj_t* obj) {
            lv_obj_set_pos(obj, x, 0);
            lv_obj_set_size(obj, fillW, bh);
          });
        }
      }

      bool hasName = g_model.limitData[channel].name[0] != 0;
      if (hasName != chanHasName) {
        chanHasName = hasName;
        setChannel();
      }
    });
  }

  static LAYOUT_VAL_SCALED(ROW_HEIGHT, 18)
  static LAYOUT_VAL_SCALED(COMPACT_ROW_HEIGHT, 15)

 protected:
  uint8_t channel;
  LcdFlags txtColor;
  LcdFlags barColor;
  coord_t rowHeight;
  bool compactTopBar = false;
  int16_t lastValue = OUTPUT_INVALID_VALUE;
  int16_t lastScaledValue = OUTPUT_INVALID_VALUE;
  std::string lastText;
  bool chanHasName = false;
  RequiredLvObj rowBox;
  RequiredLvObj valueLabel;
  RequiredLvObj chanLabel;
  RequiredLvObj track;
  RequiredLvObj bar;
  Messaging refreshMsg;
};

class OutputsWidget : public NativeWidget
{
 public:
  OutputsWidget(const WidgetFactory* factory, Window* parent,
                const rect_t& rect, WidgetLocation location) :
      NativeWidget(factory, parent, rect, location)
  {
  }

  void createContent(lv_obj_t* parent) override
  {
    (void)parent;
    initRequiredLvObj(
        fallbackTitle,
        [](lv_obj_t* parent) { return etx_label_create(parent); },
        [&](lv_obj_t* obj) {
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_label_set_text(obj, "Outputs");
        });
    initRequiredLvObj(
        fallbackHint,
        [](lv_obj_t* parent) {
          return etx_label_create(parent, FONT_XXS_INDEX);
        },
        [&](lv_obj_t* obj) {
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_label_set_text(obj, "Need larger zone");
        });
  }

  void layoutContent(const rect_t& content) override
  {
    auto widgetData = getPersistentData();

    bool compact = isCompactTopBarWidget();
    if ((!compact && (content.h <= SHOW_MIN_H || content.w <= SHOW_MIN_W)) ||
        (compact && (height() < ChannelValue::ROW_HEIGHT ||
                     width() <= COMPACT_SHOW_MIN_W)))
      return;

    bool tooSmall = usesCardChrome() && content.h < MIN_USEFUL_H;
    fallbackTitle.with([&](lv_obj_t* obj) { setObjVisible(obj, tooSmall); });
    fallbackHint.with([&](lv_obj_t* obj) { setObjVisible(obj, tooSmall); });
    if (tooSmall) {
      clearRows();
      fallbackTitle.with([&](lv_obj_t* obj) {
        layoutText(obj, content, FONT_BOLD_INDEX, primaryTextColor(),
                   LV_TEXT_ALIGN_CENTER);
      });
      fallbackHint.with([&](lv_obj_t* obj) {
        rect_t hint = content;
        hint.y = content.y + content.h / 2;
        hint.h = content.h / 2;
        layoutText(obj, hint, FONT_XXS_INDEX, mutedTextColor(),
                   LV_TEXT_ALIGN_CENTER);
      });
      return;
    }
    fallbackTitle.with([&](lv_obj_t* obj) { setObjVisible(obj, false); });
    fallbackHint.with([&](lv_obj_t* obj) { setObjVisible(obj, false); });

    bool changed = false;

    // Native cards use the design-system text color, not legacy theme whites.
    LcdFlags f = compact ? widgetData->options[4].value.unsignedValue
                         : COLOR2FLAGS(COLOR_BLACK_INDEX);
    if (compact && f == COLOR2FLAGS(COLOR_THEME_PRIMARY1_INDEX))
      f = COLOR2FLAGS(COLOR_THEME_PRIMARY2_INDEX);
    if (f != txtColor) {
      txtColor = f;
      changed = true;
    }
    f = widgetData->options[5].value.unsignedValue;
    if (compact && f == COLOR2FLAGS(COLOR_THEME_SECONDARY1_INDEX))
      f = COLOR2FLAGS(COLOR_THEME_SECONDARY2_INDEX);
    if (f != barColor) {
      barColor = f;
      changed = true;
    }

    // Setup channels
    uint8_t chan = widgetData->options[0].value.unsignedValue;
    if (chan != firstChan) {
      firstChan = chan;
      changed = true;
    }
    chan = widgetData->options[1].value.unsignedValue;
    if (chan != lastChan) {
      lastChan = chan;
      changed = true;
    }

    // Get size
    if (content.w != lastWidth) {
      lastWidth = content.w;
      changed = true;
    }
    if (content.h != lastHeight) {
      lastHeight = content.h;
      changed = true;
    }
    bool shortCard = usesCardChrome() && content.h < 58;
    coord_t rowH = ChannelValue::rowHeightFor(compact || shortCard);
    coord_t rowGap = compact ? 0 : (shortCard ? 1 : PAD_TINY);
    coord_t rowStep = rowH + rowGap;
    uint8_t n = 0;
    if (shortCard) {
      n = content.h >= 2 * rowH + rowGap ? 2 : 1;
    } else {
      n = rowStep > 0 ? lastHeight / rowStep : 0;
      if (n == 0 && lastHeight >= ChannelValue::ROW_HEIGHT) n = 1;
    }
    if (n != rows) {
      rows = n;
      changed = true;
    }
    n = (!compact && lastWidth >= SHORT_COLS_MIN_W && rows <= 2) ? 2 : 1;
    if (!shortCard)
      n = (compact || lastWidth <= COLS_MIN_W || rows < 3) ? 1 : 2;
    if (n != cols) {
      cols = n;
      changed = true;
    }

    if (changed) {
      clearRows();
      coord_t colWidth = lastWidth / cols;
      uint8_t chan = firstChan;
      for (uint8_t c = 0; c < cols && chan <= lastChan; c += 1) {
        for (uint8_t r = 0; r < rows && chan <= lastChan; r += 1, chan += 1) {
          rect_t rowRect = {
              static_cast<coord_t>(content.x + c * colWidth),
              static_cast<coord_t>(content.y + r * rowStep),
              static_cast<coord_t>(colWidth - (cols > 1 ? PAD_SMALL : 0)),
              rowH};
          if (channelWidgetCount < MAX_OUTPUT_CHANNELS) {
            channelWidgets[channelWidgetCount] = new (std::nothrow)
                ChannelValue(this, rowRect, chan - 1, txtColor, barColor, rowH);
            if (channelWidgets[channelWidgetCount]) channelWidgetCount += 1;
          }
        }
      }
    }

    invalidateNativeRefresh();
  }

  uint32_t contentRefreshKey() override
  {
    auto widgetData = getPersistentData();
    uint8_t first = widgetData->options[0].value.unsignedValue;
    uint8_t last = widgetData->options[1].value.unsignedValue;
    if (first < 1) first = 1;
    if (last > MAX_OUTPUT_CHANNELS) last = MAX_OUTPUT_CHANNELS;

    WidgetRefreshKey key;
    key.add((uint32_t)first)
        .add((uint32_t)last)
        .add((uint32_t)g_eeGeneral.ppmunit)
        .add((bool)g_model.extendedLimits);

    for (uint8_t channel = first; channel <= last; channel += 1) {
      key.add((int32_t)getChannelOutput(channel - 1))
          .addBytes(g_model.limitData[channel - 1].name, LEN_CHANNEL_NAME);
    }

    return key.value();
  }

  void refreshContent() override
  {
    Messaging::send(Messaging::REFRESH_OUTPUTS_WIDGET);
  }

  static const WidgetOption options[];

 protected:
  coord_t lastWidth = -1;
  coord_t lastHeight = -1;
  uint8_t firstChan = 255;
  uint8_t lastChan = 255;
  uint8_t cols = 0;
  uint8_t rows = 0;
  LcdFlags txtColor = 0;
  LcdFlags barColor = 0;
  RequiredLvObj fallbackTitle;
  RequiredLvObj fallbackHint;
  ChannelValue* channelWidgets[MAX_OUTPUT_CHANNELS] = {};
  uint8_t channelWidgetCount = 0;

  void clearRows()
  {
    for (uint8_t i = 0; i < channelWidgetCount; i += 1) {
      if (channelWidgets[i]) channelWidgets[i]->deleteLater();
      channelWidgets[i] = nullptr;
    }
    channelWidgetCount = 0;
  }

  static LAYOUT_VAL_SCALED(SHOW_MIN_W, 100) static LAYOUT_VAL_SCALED(SHOW_MIN_H, 20) static LAYOUT_VAL_SCALED(
      COMPACT_SHOW_MIN_W,
      36) static LAYOUT_VAL_SCALED(COLS_MIN_W,
                                   300) static LAYOUT_VAL_SCALED(SHORT_COLS_MIN_W,
                                                                 150) static LAYOUT_VAL_SCALED(MIN_USEFUL_H,
                                                                                               24)
};

const WidgetOption OutputsWidget::options[] = {
    {STR_FIRST_CHANNEL, WidgetOption::Integer, {1}, {1}, {MAX_OUTPUT_CHANNELS}},
    {STR_LAST_CHANNEL,
     WidgetOption::Integer,
     {MAX_OUTPUT_CHANNELS},
     {1},
     {MAX_OUTPUT_CHANNELS}},
    {STR_FILL_BACKGROUND, WidgetOption::Bool, false},
    {STR_BG_COLOR, WidgetOption::Color,
     COLOR2FLAGS(COLOR_THEME_SECONDARY3_INDEX)},
    {STR_TEXT_COLOR, WidgetOption::Color,
     COLOR2FLAGS(COLOR_THEME_PRIMARY1_INDEX)},
    {STR_COLOR, WidgetOption::Color, COLOR2FLAGS(COLOR_THEME_SECONDARY1_INDEX)},
    {nullptr, WidgetOption::Bool}};

// Note: Must be a template class otherwise the linker will discard the
// 'outputsWidget' object
template <class T>
class OutputsWidgetFactory : public WidgetFactory
{
 public:
  OutputsWidgetFactory(const char* name, const WidgetOption* options,
                       const char* displayName = nullptr) :
      WidgetFactory(name, options, displayName)
  {
  }

  Widget* createNew(Window* parent, const rect_t& rect,
                    WidgetLocation location) const override
  {
    return new (std::nothrow) T(this, parent, rect, location);
  }

  // Fix the options loaded from the model file to account for
  // addition of the 'last channel' option
  const void checkOptions(const WidgetLocation& location) const override
  {
    auto widgetData = location.persistentData();
    if (widgetData && widgetData->options.size() >= 4) {
      if (widgetData->options[1].type == WOV_Bool) {
        widgetData->options[5] = widgetData->options[4];
        widgetData->options[4] = widgetData->options[3];
        widgetData->options[3] = widgetData->options[2];
        widgetData->options[2] = widgetData->options[1];
        widgetData->options[1].type = WOV_Signed;
        widgetData->options[1].value.signedValue = MAX_OUTPUT_CHANNELS;
        widgetData->markChanged();
        storageDirty(location.isTopBar() ? EE_GENERAL : EE_MODEL);
      }
    }
  }
};

OutputsWidgetFactory<OutputsWidget> outputsWidget("Outputs",
                                                  OutputsWidget::options,
                                                  STR_WIDGET_OUTPUTS);
