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

#include <memory>

#include "widget.h"

#include "edgetx.h"
#include "static.h"

#define ETX_STATE_BG_FILL LV_STATE_USER_1

class ModelBitmapWidget : public TrackedWidget
{
 public:
  ModelBitmapWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
                    WidgetLocation location) :
      TrackedWidget(factory, parent, rect, location, LoadMode::Delayed)
  {
    addStyle(styles->bg_opacity_transparent, LV_PART_MAIN);
    addStyle(styles->bg_opacity_cover, LV_PART_MAIN | ETX_STATE_BG_FILL);
  }

  void delayedInit() override
  {
    if (!initRequiredWindow(label, this, rect_t{}, "")) return;
    label.withLive([](LiveWindow& live) {
      lv_label_set_long_mode(live.lvobj(), LV_LABEL_LONG_DOT);
    });
    label.with([](StaticText& text) { text.hide(); });

    if (!initRequiredWindow(image, this, rect_t{0, 0, width(), height()})) return;
    image.with([](StaticBitmap& bitmap) { bitmap.hide(); });

    foreground();
  }

  uint32_t refreshKey() override
  {
    WidgetRefreshKey key;
    key.addBytes(g_model.header.name, LEN_MODEL_NAME)
       .add(getHash());
    return key.value();
  }

  void refresh() override
  {
    char s[LEN_MODEL_NAME + 1];
    strAppend(s, g_model.header.name, LEN_MODEL_NAME);
    bool textChanged = label.valueOr(true, [&](StaticText& text) {
      bool changed = text.getText() != s;
      if (changed) text.setText(s);
      return changed;
    });

    if (textChanged || getHash() != deps_hash) {
      update();
    }
  }

  void onUpdate() override
  {
    auto widgetData = getPersistentData();

    bool hasBitmap = g_model.header.bitmap[0] != '\0';
    isLarge = rect.h >= LARGE_H && rect.w >= LARGE_W;

    // set font colour from options[0], if use theme color option off
    label.withLive([&](LiveWindow& live) {
      if (widgetData->options[4].value.boolValue) {
        etx_txt_color(live.lvobj(),
                      isTopBarWidget() ? COLOR_THEME_PRIMARY2_INDEX
                                       : COLOR_THEME_SECONDARY1_INDEX,
                      LV_PART_MAIN);
      } else {
        etx_txt_color_from_flags(live.lvobj(),
                                 widgetData->options[0].value.unsignedValue);
      }
    });

    coord_t labelHeight = isLarge && hasBitmap ? LARGE_IMG_H : height();
    rect_t labelRect = {0, 0, width(), labelHeight};
    FontIndex font = responsiveTextFont(labelRect.h);
    label.withLive([&](LiveWindow& live) {
      layoutTextLabel(live.lvobj(), labelRect, font);
    });

    // get fill color from options[3]
    withLive([&](LiveWindow& live) {
      etx_bg_color_from_flags(live.lvobj(),
                              widgetData->options[3].value.unsignedValue);

      // Set background opacity from options[2]
      if (widgetData->options[2].value.boolValue)
        lv_obj_add_state(live.lvobj(), ETX_STATE_BG_FILL);
      else
        lv_obj_clear_state(live.lvobj(), ETX_STATE_BG_FILL);
    });

    coord_t w = width();
    coord_t h = height() - (isLarge && hasBitmap ? LARGE_IMG_H : 0);
    bool sizeChg = image.valueOr(true, [&](StaticBitmap& bitmap) {
      return w != bitmap.width() || h != bitmap.height();
    });

    if (sizeChg)
      image.with([&](StaticBitmap& bitmap) {
        bitmap.setRect({0, isLarge && hasBitmap ? LARGE_IMG_H : 0, w, h});
      });

    bool hasImage = image.valueOr(false,
                                  [](StaticBitmap& bitmap) { return bitmap.hasImage(); });
    if (!hasImage || deps_hash != getHash() || sizeChg) {
      if (g_model.header.bitmap[0]) {
        char filename[LEN_BITMAP_NAME + 1];
        strAppend(filename, g_model.header.bitmap, LEN_BITMAP_NAME);
        std::string fullpath =
            std::string(BITMAPS_PATH PATH_SEPARATOR) + filename;

        image.with([&](StaticBitmap& bitmap) { bitmap.setSource(fullpath.c_str()); });
      } else {
        image.with([](StaticBitmap& bitmap) { bitmap.clearSource(); });
      }
      deps_hash = getHash();
    }

    hasImage = image.valueOr(false,
                             [](StaticBitmap& bitmap) { return bitmap.hasImage(); });
    image.with([&](StaticBitmap& bitmap) { bitmap.show(hasImage); });

    label.with([&](StaticText& text) { text.show(isLarge || !hasImage); });
  }

  static const WidgetOption options[];

 protected:
  bool isLarge = false;
  uint32_t deps_hash = 0;
  RequiredWindow<StaticText> label;
  RequiredWindow<StaticBitmap> image;

  uint32_t getHash() { return hash(g_model.header.bitmap, LEN_BITMAP_NAME); }

  static LAYOUT_VAL_SCALED(LARGE_W, 120)
  static LAYOUT_VAL_SCALED(LARGE_H, 96)
  static LAYOUT_VAL_SCALED(LARGE_LBL_X, 5)
  static LAYOUT_VAL_SCALED(LARGE_LBL_Y, 5)
  static LAYOUT_VAL_SCALED(LARGE_IMG_H, 38)
};

const WidgetOption ModelBitmapWidget::options[] = {
    {STR_COLOR, WidgetOption::Color, COLOR2FLAGS(COLOR_THEME_SECONDARY1_INDEX)},
    {"", WidgetOption::TextSize, FONT_STD_INDEX},
    {STR_FILL_BACKGROUND, WidgetOption::Bool, false},
    {STR_BG_COLOR, WidgetOption::Color, COLOR2FLAGS(COLOR_THEME_SECONDARY3_INDEX)},
    {STR_USE_THEME_COLOR, WidgetOption::Bool, true},
    {nullptr, WidgetOption::Bool}};

BaseWidgetFactory<ModelBitmapWidget> modelBitmapWidget(
    "ModelBmp", ModelBitmapWidget::options, STR_WIDGET_MODELBMP);
