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
#include "hal/usb_driver.h"
#include "layout.h"
#include "theme_manager.h"

#include <new>
#include <stdio.h>

namespace {

constexpr uint8_t LINK_BARS = 5;
const uint8_t linkBarThresholds[LINK_BARS] = {30, 40, 50, 60, 80};

struct StatusContentBox {
  coord_t x;
  coord_t y;
  coord_t w;
  coord_t h;
};

coord_t minCoord(coord_t a, coord_t b) { return a < b ? a : b; }
coord_t maxCoord(coord_t a, coord_t b) { return a > b ? a : b; }

coord_t clampCoord(coord_t value, coord_t low, coord_t high)
{
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

constexpr coord_t TOPBAR_CONTENT_PAD = PAD_TINY;

StatusContentBox topbarContentBox(coord_t w, coord_t h)
{
  coord_t contentW = w > 2 * TOPBAR_CONTENT_PAD
                         ? w - 2 * TOPBAR_CONTENT_PAD
                         : maxCoord(w, (coord_t)1);
  coord_t contentH = h > 2 * TOPBAR_CONTENT_PAD
                         ? h - 2 * TOPBAR_CONTENT_PAD
                         : maxCoord(h, (coord_t)1);
  return {TOPBAR_CONTENT_PAD, TOPBAR_CONTENT_PAD, contentW, contentH};
}

lv_obj_t* makeStatusPart(lv_obj_t* parent)
{
  auto obj = lv_obj_create(parent);
  if (!obj) return nullptr;

  lv_obj_remove_style_all(obj);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  return obj;
}

LcdColorIndex statusPrimaryColor(bool topbar)
{
  return topbar ? COLOR_THEME_PRIMARY2_INDEX : COLOR_THEME_SECONDARY1_INDEX;
}

LcdColorIndex statusMutedColor(bool topbar)
{
  return topbar ? COLOR_THEME_SECONDARY2_INDEX : COLOR_THEME_SECONDARY2_INDEX;
}

void setStatusPartColor(lv_obj_t* obj, LcdColorIndex color)
{
  if (!obj) return;
  etx_solid_bg(obj, color);
}

void setStatusPartBorder(lv_obj_t* obj, LcdColorIndex color, coord_t width)
{
  if (!obj) return;
  etx_border_color(obj, color);
  lv_obj_set_style_border_width(obj, width, LV_PART_MAIN);
}

void setStatusLabel(lv_obj_t* label, const char* text, LcdColorIndex color,
                    FontIndex font, coord_t x, coord_t y, coord_t w, coord_t h)
{
  if (!label) return;

  etx_font(label, font);
  etx_txt_color(label, color);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_size(label, w, h);
}

void setLvVisible(lv_obj_t* obj, bool visible)
{
  if (!obj) return;
  if (visible)
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

uint8_t activeLinkBars(uint8_t rssi)
{
  uint8_t bars = 0;
  for (uint8_t i = 0; i < LINK_BARS; i += 1) {
    if (rssi >= linkBarThresholds[i]) bars = i + 1;
  }
  return bars;
}

bool linkWarning(uint8_t rssi)
{
  return rssi > 0 && rssi < (uint8_t)g_model.rfAlarms.warning;
}

uint8_t speakerVolumeLevel()
{
#if defined(AUDIO)
  if (requiredSpeakerVolume == 0 || g_eeGeneral.beepMode == e_mode_quiet)
    return 0;
  if (requiredSpeakerVolume < 7)
    return 1;
  if (requiredSpeakerVolume < 13)
    return 2;
  if (requiredSpeakerVolume < 19)
    return 3;
  return 4;
#else
  return 0;
#endif
}

uint8_t speakerVolumePercent()
{
#if defined(AUDIO)
  if (g_eeGeneral.beepMode == e_mode_quiet) return 0;
  return divRoundClosest((uint16_t)requiredSpeakerVolume * 100, VOLUME_LEVEL_MAX);
#else
  return 0;
#endif
}

}  // namespace

class LinkStatusWidget : public Widget
{
 public:
  LinkStatusWidget(const WidgetFactory* factory, Window* parent,
                   const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    for (uint8_t i = 0; i < LINK_BARS; i += 1) {
      bars[i] = makeStatusPart(lvobj);
      if (bars[i]) lv_obj_set_style_radius(bars[i], 1, LV_PART_MAIN);
    }

    title = etx_label_create(lvobj, FONT_XXS_INDEX);
    value = etx_label_create(lvobj, FONT_BOLD_INDEX);

    update();
    foreground();
  }

  void update() override
  {
    if (_deleted) return;

    const bool topbar = isCompactTopBarWidget();
    const coord_t pad = topbar ? TOPBAR_CONTENT_PAD : PAD_SMALL;
    const coord_t labelW = width() > 2 * pad ? width() - 2 * pad : width();

    setLvVisible(title, !topbar && height() >= 54);
    setLvVisible(value, !topbar);

    coord_t graphX = pad;
    coord_t graphY = pad;
    coord_t graphW = width() > 2 * pad ? width() - 2 * pad : width();
    coord_t graphH = height() > 2 * pad ? height() - 2 * pad : height();

    if (topbar) {
      auto box = topbarContentBox(width(), height());
      graphX = box.x;
      graphY = box.y;
      graphW = box.w;
      graphH = box.h;
    } else {
      coord_t textW = maxCoord(labelW / 3, (coord_t)34);
      if (width() > 110) {
        graphW = minCoord((coord_t)(width() - textW - 3 * pad), (coord_t)58);
      } else {
        graphW = minCoord(graphW, (coord_t)58);
      }
      graphH = minCoord(graphH, height() >= 72 ? (coord_t)42 : (coord_t)32);
      graphY = (height() - graphH) / 2;

      coord_t textX = graphX + graphW + pad;
      coord_t textRight = width() > pad ? width() - pad : width();
      if (textX + 22 < textRight) {
        coord_t textAreaW = textRight - textX;
        coord_t titleH = height() >= 54 ? EdgeTxStyles::STD_FONT_HEIGHT / 2 : 0;
        FontIndex valueFont = responsiveTextFont(height() - 2 * pad - titleH);
        coord_t valueH = getFontHeight(LcdFlags(valueFont) << 8u);
        coord_t textBlockH = titleH + valueH;
        coord_t textY = (height() - textBlockH) / 2;

        setStatusLabel(title, "LINK", COLOR_THEME_PRIMARY1_INDEX,
                       FONT_XXS_INDEX, textX, textY, textAreaW, titleH);
        setStatusLabel(value, "--", statusPrimaryColor(false), valueFont,
                       textX, textY + titleH, textAreaW, valueH);
      } else {
        setLvVisible(title, false);
        setLvVisible(value, false);
      }
    }

    coord_t gap = topbar ? TOPBAR_CONTENT_PAD : PAD_THREE;
    coord_t barW = (graphW - (LINK_BARS - 1) * gap) / LINK_BARS;
    if (topbar)
      barW = maxCoord(barW, (coord_t)1);
    else
      barW = clampCoord(barW, (coord_t)6, (coord_t)14);

    for (uint8_t i = 0; i < LINK_BARS; i += 1) {
      if (!bars[i]) continue;
      coord_t barH = ((i + 1) * graphH + LINK_BARS - 1) / LINK_BARS;
      if (i == 0) barH = maxCoord(barH, topbar ? (coord_t)8 : (coord_t)9);
      lv_obj_set_pos(bars[i], graphX + i * (barW + gap),
                     graphY + graphH - barH);
      lv_obj_set_size(bars[i], barW, barH);
    }

    lastRSSI = 255;
  }

  void foreground() override
  {
    if (_deleted) return;

    uint8_t rssi = TELEMETRY_RSSI();
    if (rssi == lastRSSI) return;
    lastRSSI = rssi;

    bool topbar = isCompactTopBarWidget();
    bool warning = linkWarning(rssi);
    uint8_t active = activeLinkBars(rssi);
    LcdColorIndex activeColor = warning ? COLOR_THEME_WARNING_INDEX
                                        : statusPrimaryColor(topbar);
    LcdColorIndex mutedColor = statusMutedColor(topbar);

    for (uint8_t i = 0; i < LINK_BARS; i += 1) {
      setStatusPartColor(bars[i], i < active ? activeColor : mutedColor);
    }

    if (value && !topbar) {
      char text[8];
      if (rssi == 0)
        snprintf(text, sizeof(text), "--");
      else
        snprintf(text, sizeof(text), "%u", rssi);

      FontIndex font = responsiveTextFont(height() - 2 * PAD_SMALL -
                                          EdgeTxStyles::STD_FONT_HEIGHT / 2);
      setStatusLabel(value, text,
                     warning ? COLOR_THEME_WARNING_INDEX
                             : statusPrimaryColor(false),
                     font, lv_obj_get_x(value), lv_obj_get_y(value),
                     lv_obj_get_width(value), lv_obj_get_height(value));
    }
  }

 protected:
  lv_obj_t* bars[LINK_BARS] = {};
  lv_obj_t* title = nullptr;
  lv_obj_t* value = nullptr;
  uint8_t lastRSSI = 255;
};

BaseWidgetFactory<LinkStatusWidget> linkStatusWidget("Link", nullptr, "Link");

class TxBatteryStatusWidget : public Widget
{
 public:
  TxBatteryStatusWidget(const WidgetFactory* factory, Window* parent,
                        const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    shell = makeStatusPart(lvobj);
    fill = makeStatusPart(lvobj);
    cap = makeStatusPart(lvobj);
    title = etx_label_create(lvobj, FONT_XXS_INDEX);
    value = etx_label_create(lvobj, FONT_BOLD_INDEX);

    if (shell) {
      lv_obj_set_style_bg_opa(shell, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_radius(shell, 3, LV_PART_MAIN);
    }
    if (fill) lv_obj_set_style_radius(fill, 2, LV_PART_MAIN);
    if (cap) lv_obj_set_style_radius(cap, 1, LV_PART_MAIN);

    update();
    foreground();
  }

  void update() override
  {
    if (_deleted) return;

    const bool topbar = isCompactTopBarWidget();
    const coord_t pad = topbar ? TOPBAR_CONTENT_PAD : PAD_SMALL;

    setLvVisible(title, !topbar && height() >= 54);
    setLvVisible(value, !topbar);

    coord_t capW = PAD_THREE;
    coord_t battW = 0;
    coord_t battH = 0;
    coord_t battX = pad;
    coord_t battY = 0;

    if (topbar) {
      auto box = topbarContentBox(width(), height());
      capW = minCoord(PAD_THREE, maxCoord((coord_t)(box.w / 8), (coord_t)1));
      battW = maxCoord((coord_t)(box.w - capW), (coord_t)1);
      battH = minCoord(box.h, maxCoord((coord_t)(battW / 2), (coord_t)1));
      battX = box.x;
      battY = box.y + (box.h - battH) / 2;
    } else {
      battH = minCoord((coord_t)30, height() - 2 * pad);
      battH = maxCoord(battH, (coord_t)18);
      battW = minCoord((coord_t)58, width() - 3 * pad);
      battW = maxCoord(battW, (coord_t)36);
      battY = (height() - battH) / 2;

      if (width() < 110) {
        battW = minCoord(battW, (coord_t)(width() - 3 * pad));
      }
    }

    if (shell) {
      lv_obj_set_pos(shell, battX, battY);
      lv_obj_set_size(shell, battW, battH);
      setStatusPartBorder(shell, statusPrimaryColor(topbar), 2);
    }
    if (cap) {
      lv_obj_set_pos(cap, battX + battW, battY + battH / 4);
      lv_obj_set_size(cap, capW, battH / 2);
    }

    coord_t fillInset = PAD_THREE;
    coord_t fillX = battX + fillInset;
    coord_t fillY = battY + fillInset;
    fillMaxW = battW > 2 * fillInset ? battW - 2 * fillInset : battW;
    fillH = battH > 2 * fillInset ? battH - 2 * fillInset : battH;

    if (fill) {
      lv_obj_set_pos(fill, fillX, fillY);
      lv_obj_set_size(fill, fillMaxW, fillH);
    }

    if (!topbar) {
      coord_t textX = battX + battW + PAD_SMALL + 3;
      coord_t textRight = width() > pad ? width() - pad : width();
      if (textX + 24 < textRight) {
        coord_t titleH = height() >= 54 ? EdgeTxStyles::STD_FONT_HEIGHT / 2 : 0;
        FontIndex valueFont = responsiveTextFont(height() - 2 * pad - titleH);
        coord_t valueH = getFontHeight(LcdFlags(valueFont) << 8u);
        coord_t textBlockH = titleH + valueH;
        coord_t textY = (height() - textBlockH) / 2;
        coord_t textW = textRight - textX;

        setStatusLabel(title, "TX BAT", COLOR_THEME_PRIMARY1_INDEX,
                       FONT_XXS_INDEX, textX, textY, textW, titleH);
        setStatusLabel(value, "", statusPrimaryColor(false), valueFont,
                       textX, textY + titleH, textW, valueH);
      } else {
        setLvVisible(title, false);
        setLvVisible(value, false);
      }
    }

    lastBattBars = 255;
    lastVoltage = 255;
  }

  void foreground() override
  {
    if (_deleted) return;

    bool topbar = isCompactTopBarWidget();
    bool warning = IS_TXBATT_WARNING();
    uint8_t bars = GET_TXBATT_BARS(fillMaxW);
    LcdColorIndex color = warning ? COLOR_THEME_WARNING_INDEX
                                  : statusPrimaryColor(topbar);

    setStatusPartBorder(shell, color, 2);
    setStatusPartColor(cap, color);

    if (bars != lastBattBars) {
      lastBattBars = bars;
      if (fill) {
        lv_obj_set_size(fill, bars, fillH);
      }
    }
    setStatusPartColor(fill, color);

    if (value && !topbar && g_vbat100mV != lastVoltage) {
      lastVoltage = g_vbat100mV;

      char text[10];
      snprintf(text, sizeof(text), "%u.%uV", g_vbat100mV / 10,
               g_vbat100mV % 10);

      setStatusLabel(value, text,
                     warning ? COLOR_THEME_WARNING_INDEX
                             : statusPrimaryColor(false),
                     responsiveTextFont(height() - 2 * PAD_SMALL -
                                        EdgeTxStyles::STD_FONT_HEIGHT / 2),
                     lv_obj_get_x(value), lv_obj_get_y(value),
                     lv_obj_get_width(value), lv_obj_get_height(value));
    }
  }

 protected:
  lv_obj_t* shell = nullptr;
  lv_obj_t* fill = nullptr;
  lv_obj_t* cap = nullptr;
  lv_obj_t* title = nullptr;
  lv_obj_t* value = nullptr;
  coord_t fillMaxW = 1;
  coord_t fillH = 1;
  uint8_t lastBattBars = 255;
  uint8_t lastVoltage = 255;
};

BaseWidgetFactory<TxBatteryStatusWidget> txBatteryStatusWidget("TX Battery",
                                                               nullptr,
                                                               "TX Battery");

#if defined(AUDIO)

class VolumeStatusWidget : public Widget
{
 public:
  VolumeStatusWidget(const WidgetFactory* factory, Window* parent,
                     const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    icon = new (std::nothrow) StaticIcon(this, 0, 0, ICON_TOPMENU_VOLUME_0,
                                         COLOR_THEME_PRIMARY2_INDEX);

    for (uint8_t i = 0; i < VOLUME_SEGMENTS; i += 1) {
      segments[i] = makeStatusPart(lvobj);
      if (segments[i]) lv_obj_set_style_radius(segments[i], 1, LV_PART_MAIN);
    }

    title = etx_label_create(lvobj, FONT_XXS_INDEX);
    value = etx_label_create(lvobj, FONT_BOLD_INDEX);

    update();
    foreground();
  }

  void update() override
  {
    if (_deleted) return;

    const bool topbar = isCompactTopBarWidget();
    const coord_t pad = topbar ? TOPBAR_CONTENT_PAD : PAD_SMALL;

    setLvVisible(title, !topbar && height() >= 54);
    setLvVisible(value, !topbar);

    StatusContentBox box = topbar ? topbarContentBox(width(), height())
                                  : StatusContentBox{pad, pad,
                                                     width() > 2 * pad
                                                         ? width() - 2 * pad
                                                         : width(),
                                                     height() > 2 * pad
                                                         ? height() - 2 * pad
                                                         : height()};

    if (icon) {
      icon->setColor(statusPrimaryColor(topbar));
      icon->setPos(box.x, (height() - icon->height()) / 2);
    }

    coord_t gap = topbar ? TOPBAR_CONTENT_PAD : PAD_TINY;
    coord_t segX = icon ? icon->left() + icon->width() + gap : box.x;
    coord_t segAreaRight = topbar ? box.x + box.w
                                  : (width() > pad ? width() - pad : width());
    coord_t segAreaW = segAreaRight > segX ? segAreaRight - segX : 0;
    coord_t segW = (segAreaW - (VOLUME_SEGMENTS - 1) * gap) / VOLUME_SEGMENTS;
    if (topbar)
      segW = maxCoord(segW, (coord_t)1);
    else
      segW = clampCoord(segW, (coord_t)4, (coord_t)10);
    coord_t segH = topbar ? maxCoord((coord_t)(box.h - 2 * TOPBAR_CONTENT_PAD),
                                     (coord_t)1)
                          : minCoord((coord_t)22, height() - 2 * pad);
    if (!topbar) segH = maxCoord(segH, (coord_t)8);
    coord_t segY = (height() - segH) / 2;

    for (uint8_t i = 0; i < VOLUME_SEGMENTS; i += 1) {
      if (!segments[i]) continue;
      lv_obj_set_pos(segments[i], segX + i * (segW + gap), segY);
      lv_obj_set_size(segments[i], segW, segH);
    }

    if (!topbar) {
      coord_t textX = segX + VOLUME_SEGMENTS * segW +
                      (VOLUME_SEGMENTS - 1) * gap + PAD_SMALL;
      coord_t textRight = width() > pad ? width() - pad : width();
      if (textX + 24 < textRight) {
        coord_t titleH = height() >= 54 ? EdgeTxStyles::STD_FONT_HEIGHT / 2 : 0;
        FontIndex valueFont = responsiveTextFont(height() - 2 * pad - titleH);
        coord_t valueH = getFontHeight(LcdFlags(valueFont) << 8u);
        coord_t textBlockH = titleH + valueH;
        coord_t textY = (height() - textBlockH) / 2;
        coord_t textW = textRight - textX;

        setStatusLabel(title, "VOL", COLOR_THEME_PRIMARY1_INDEX,
                       FONT_XXS_INDEX, textX, textY, textW, titleH);
        setStatusLabel(value, "", statusPrimaryColor(false), valueFont,
                       textX, textY + titleH, textW, valueH);
      } else {
        setLvVisible(title, false);
        setLvVisible(value, false);
      }
    }

    lastLevel = 255;
    lastPercent = 255;
  }

  void foreground() override
  {
    if (_deleted) return;

    bool topbar = isCompactTopBarWidget();
    uint8_t level = speakerVolumeLevel();
    uint8_t percent = speakerVolumePercent();
    if (level == lastLevel && percent == lastPercent) return;

    lastLevel = level;
    lastPercent = percent;

    if (icon) {
      icon->setIcon((EdgeTxIcon)(ICON_TOPMENU_VOLUME_0 + level));
      icon->setColor(level == 0 ? statusMutedColor(topbar)
                                : statusPrimaryColor(topbar));
    }

    for (uint8_t i = 0; i < VOLUME_SEGMENTS; i += 1) {
      setStatusPartColor(segments[i],
                         i < level ? statusPrimaryColor(topbar)
                                   : statusMutedColor(topbar));
    }

    if (value && !topbar) {
      char text[10];
      if (level == 0)
        snprintf(text, sizeof(text), "MUTE");
      else
        snprintf(text, sizeof(text), "%u%%", percent);

      setStatusLabel(value, text,
                     level == 0 ? COLOR_THEME_DISABLED_INDEX
                                : statusPrimaryColor(false),
                     responsiveTextFont(height() - 2 * PAD_SMALL -
                                        EdgeTxStyles::STD_FONT_HEIGHT / 2),
                     lv_obj_get_x(value), lv_obj_get_y(value),
                     lv_obj_get_width(value), lv_obj_get_height(value));
    }
  }

 protected:
  static constexpr uint8_t VOLUME_SEGMENTS = 4;
  StaticIcon* icon = nullptr;
  lv_obj_t* segments[VOLUME_SEGMENTS] = {};
  lv_obj_t* title = nullptr;
  lv_obj_t* value = nullptr;
  uint8_t lastLevel = 255;
  uint8_t lastPercent = 255;
};

BaseWidgetFactory<VolumeStatusWidget> volumeStatusWidget("Volume", nullptr,
                                                         STR_VOLUME);

#endif

class RadioInfoWidget : public Widget
{
 public:
  RadioInfoWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
                  int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    bool compact = isCompactTopBarWidget();

    // Logs
    logsIcon = new (std::nothrow) StaticIcon(this, W_LOG_X, PAD_THREE, ICON_DOT,
                                             COLOR_THEME_PRIMARY2_INDEX);
    if (logsIcon) logsIcon->hide();

    usbIcon =
        new (std::nothrow) StaticIcon(this, W_USB_X, W_USB_Y, ICON_TOPMENU_USB,
                                      COLOR_THEME_PRIMARY2_INDEX);
    if (usbIcon) usbIcon->hide();

#if defined(AUDIO)
    if (!compact) {
      audioScale = new (std::nothrow) StaticIcon(this, W_AUDIO_SCALE_X, PAD_TINY,
                                                 ICON_TOPMENU_VOLUME_SCALE,
                                                 COLOR_THEME_SECONDARY2_INDEX);

      for (int i = 0; i < 5; i += 1) {
        audioVol[i] = new (std::nothrow) StaticIcon(
            this, W_AUDIO_X, PAD_TINY,
           (EdgeTxIcon)(ICON_TOPMENU_VOLUME_0 + i),
            COLOR_THEME_PRIMARY2_INDEX);
        if (audioVol[i]) audioVol[i]->hide();
      }
      if (audioVol[0]) audioVol[0]->show();
    }
#endif

    if (!compact) {
      batteryIcon = new (std::nothrow) StaticIcon(this, W_AUDIO_X, W_BATT_Y,
                                                  ICON_TOPMENU_TXBATT,
                                                  COLOR_THEME_PRIMARY2_INDEX);
    }
#if defined(USB_CHARGER)
    if (!compact) {
      batteryChargeIcon = new (std::nothrow) StaticIcon(
          this, W_BATT_CHG_X, W_BATT_CHG_Y,
          ICON_TOPMENU_TXBATT_CHARGE, COLOR_THEME_PRIMARY2_INDEX);
      if (batteryChargeIcon) batteryChargeIcon->hide();
    }
#endif

    if (compact) {
      batteryShell = lv_obj_create(lvobj);
      if (batteryShell) {
        lv_obj_clear_flag(batteryShell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(batteryShell, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(batteryShell, 2, LV_PART_MAIN);
        lv_obj_set_style_border_opa(batteryShell, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(batteryShell,
                                      makeLvColor(COLOR_THEME_PRIMARY2),
                                      LV_PART_MAIN);
        lv_obj_set_style_radius(batteryShell, 2, LV_PART_MAIN);
      }
      batteryCap = lv_obj_create(lvobj);
      if (batteryCap) {
        lv_obj_clear_flag(batteryCap, LV_OBJ_FLAG_CLICKABLE);
        etx_solid_bg(batteryCap, COLOR_THEME_PRIMARY2_INDEX);
      }
    }

#if defined(INTERNAL_MODULE_PXX1) && defined(EXTERNAL_ANTENNA)
    if (!compact) {
      extAntenna = new (std::nothrow) StaticIcon(this, W_RSSI_X - PAD_SMALL, 1,
                                                 ICON_TOPMENU_ANTENNA,
                                                 COLOR_THEME_PRIMARY2_INDEX);
      if (extAntenna) extAntenna->hide();
    }
#endif

    batteryFill = lv_obj_create(lvobj);
    if (batteryFill) {
      lv_obj_set_style_bg_opa(batteryFill, LV_OPA_COVER, LV_PART_MAIN);
    }
    update();

    // RSSI bars
    for (unsigned int i = 0; i < DIM(rssiBars); i++) {
      rssiBars[i] = lv_obj_create(lvobj);
      if (rssiBars[i]) {
        etx_solid_bg(rssiBars[i], COLOR_THEME_SECONDARY2_INDEX);
        etx_bg_color(rssiBars[i], COLOR_THEME_PRIMARY2_INDEX, LV_STATE_USER_1);
      }
    }

    layoutStatus();
    foreground();
  }

  void update() override
  {
    if (_deleted) return;

    auto widgetData = getPersistentData();
    if (!batteryFill) return;

    layoutStatus();

    // get colors from options
    etx_bg_color_from_flags(batteryFill, widgetData->options[2].value.unsignedValue, LV_PART_MAIN);
    etx_bg_color_from_flags(batteryFill, widgetData->options[1].value.unsignedValue, LV_STATE_USER_1);
    etx_bg_color_from_flags(batteryFill, widgetData->options[0].value.unsignedValue, LV_STATE_USER_2);
  }

  void foreground() override
  {
    if (_deleted) return;

    bool compact = isCompactTopBarWidget();

    if (usbIcon) usbIcon->show(!compact && usbPlugged());
    if (getSelectedUsbMode() == USB_UNSELECTED_MODE)
      { if (usbIcon) usbIcon->setColor(COLOR_THEME_SECONDARY2_INDEX); }
    else
      { if (usbIcon) usbIcon->setColor(COLOR_THEME_PRIMARY2_INDEX); }

    if (logsIcon) {
      logsIcon->show(!compact && !usbPlugged() && isFunctionActive(FUNCTION_LOGS) &&
                     BLINK_ON_PHASE);
    }

#if defined(AUDIO)
    /* Audio volume */
    uint8_t vol = 4;
    if (requiredSpeakerVolume == 0 || g_eeGeneral.beepMode == e_mode_quiet)
      vol = 0;
    else if (requiredSpeakerVolume < 7)
      vol = 1;
    else if (requiredSpeakerVolume < 13)
      vol = 2;
    else if (requiredSpeakerVolume < 19)
      vol = 3;
    if (vol != lastVol) {
      if (audioVol[vol]) audioVol[vol]->show();
      if (audioVol[lastVol]) audioVol[lastVol]->hide();
      lastVol = vol;
    }
#endif

#if defined(USB_CHARGER)
    if (batteryChargeIcon) batteryChargeIcon->show(!compact && usbChargerLed());
#endif

#if defined(INTERNAL_MODULE_PXX1) && defined(EXTERNAL_ANTENNA)
    if (extAntenna) {
      extAntenna->show(!compact && isModuleXJT(INTERNAL_MODULE) &&
                       isExternalAntennaEnabled());
    }
#endif

    // Battery level
    uint8_t bars = GET_TXBATT_BARS(batteryFillWidth());
    if (bars != lastBatt) {
      lastBatt = bars;
      lv_obj_set_size(batteryFill, bars, batteryFillHeight());
      if (bars >= batteryGreenThreshold()) {
        lv_obj_clear_state(batteryFill, LV_STATE_USER_1 | LV_STATE_USER_2);
      } else if (bars >= batteryOrangeThreshold()) {
        lv_obj_add_state(batteryFill, LV_STATE_USER_1);
        lv_obj_clear_state(batteryFill, LV_STATE_USER_2);
      } else {
        lv_obj_clear_state(batteryFill, LV_STATE_USER_1);
        lv_obj_add_state(batteryFill, LV_STATE_USER_2);
      }
    }

    // RSSI
    const uint8_t rssiBarsValue[] = {30, 40, 50, 60, 80};
    uint8_t rssi = TELEMETRY_RSSI();
    if (rssi != lastRSSI) {
      lastRSSI = rssi;
      for (unsigned int i = 0; i < DIM(rssiBarsValue); i++) {
        if (!rssiBars[i])
          continue;
        if (rssi >= rssiBarsValue[i])
          lv_obj_add_state(rssiBars[i], LV_STATE_USER_1);
        else
          lv_obj_clear_state(rssiBars[i], LV_STATE_USER_1);
      }
    }
  }

  static const WidgetOption options[];

  static constexpr coord_t W_AUDIO_X = 0;
  static LAYOUT_VAL_SCALED(W_AUDIO_SCALE_X, 15)
  static LAYOUT_VAL_SCALED(W_USB_X, 32)
  static LAYOUT_VAL_SCALED(W_USB_Y, 5)
  static constexpr coord_t W_LOG_X = W_USB_X;
  static LAYOUT_VAL_SCALED(W_RSSI_X, 37)
  static LAYOUT_VAL_SCALED(W_RSSI_BAR_W, 5)
  static LAYOUT_VAL_SCALED(W_RSSI_BAR_H, 36)
  static LAYOUT_VAL_SCALED(W_RSSI_BAR_SZ, 7)
  static LAYOUT_VAL_SCALED(W_BATT_Y, 25)
  static LAYOUT_VAL_SCALED(W_BATT_FILL_W, 20)
  static LAYOUT_VAL_SCALED(W_BATT_FILL_H, 10)
  static LAYOUT_VAL_SCALED(W_BATT_FILL_GRN, 12)
  static LAYOUT_VAL_SCALED(W_BATT_FILL_ORA, 5)
  static LAYOUT_VAL_SCALED(W_BATT_HUD_X, 2)
  static LAYOUT_VAL_SCALED(W_BATT_HUD_W, 29)
  static LAYOUT_VAL_SCALED(W_BATT_HUD_H, 13)
  static LAYOUT_VAL_SCALED(W_BATT_HUD_FILL_W, 25)
  static LAYOUT_VAL_SCALED(W_BATT_HUD_FILL_H, 9)
  static LAYOUT_VAL_SCALED(W_BATT_HUD_FILL_GRN, 14)
  static LAYOUT_VAL_SCALED(W_BATT_HUD_FILL_ORA, 6)
  static LAYOUT_VAL_SCALED(W_BATT_CHG_X, 25)
  static LAYOUT_VAL_SCALED(W_BATT_CHG_Y, 23)
  static LAYOUT_VAL_SCALED(W_RSSI_HUD_BAR_W, 5)
  static LAYOUT_VAL_SCALED(W_RSSI_HUD_BAR_SZ, 7)

  coord_t batteryFillX() const
  {
    return isCompactTopBarWidget() ? W_BATT_HUD_X + 2 : W_AUDIO_X + 1;
  }

  coord_t batteryFillY() const
  {
    return isCompactTopBarWidget() ? batteryShellY() + 2 : W_BATT_Y + 1;
  }

  uint8_t batteryFillWidth() const
  {
    return isCompactTopBarWidget() ? W_BATT_HUD_FILL_W : W_BATT_FILL_W;
  }

  coord_t batteryFillHeight() const
  {
    return isCompactTopBarWidget() ? W_BATT_HUD_FILL_H : W_BATT_FILL_H;
  }

  uint8_t batteryGreenThreshold() const
  {
    return isCompactTopBarWidget() ? W_BATT_HUD_FILL_GRN : W_BATT_FILL_GRN;
  }

  uint8_t batteryOrangeThreshold() const
  {
    return isCompactTopBarWidget() ? W_BATT_HUD_FILL_ORA : W_BATT_FILL_ORA;
  }

  coord_t rssiX() const
  {
    if (!isCompactTopBarWidget()) return W_RSSI_X;

    coord_t minX = W_BATT_HUD_X + W_BATT_HUD_W + PAD_MEDIUM;
    coord_t alignX = width() - rssiClusterWidth();
    return alignX > minX ? alignX : minX;
  }

  coord_t rssiBarWidth() const
  {
    return isCompactTopBarWidget() ? W_RSSI_HUD_BAR_W : W_RSSI_BAR_W;
  }

  coord_t rssiBarStep() const
  {
    return isCompactTopBarWidget() ? W_RSSI_HUD_BAR_SZ : W_RSSI_BAR_SZ;
  }

  coord_t rssiBarHeight() const
  {
    return isCompactTopBarWidget() ? height() - 2 * PAD_TINY : (coord_t)31;
  }

  coord_t rssiBarBottom() const
  {
    return isCompactTopBarWidget() ? height() - PAD_TINY : W_RSSI_BAR_H;
  }

 protected:
  uint8_t lastVol = 0;
  uint8_t lastBatt = 0;
  uint8_t lastRSSI = 0;
  StaticIcon* logsIcon = nullptr;
  StaticIcon* usbIcon = nullptr;
#if defined(AUDIO)
  StaticIcon* audioScale = nullptr;
  StaticIcon* audioVol[5] = {};
#endif
  StaticIcon* batteryIcon = nullptr;
  lv_obj_t* batteryShell = nullptr;
  lv_obj_t* batteryCap = nullptr;
  lv_obj_t* batteryFill = nullptr;
  lv_obj_t* rssiBars[5] = {nullptr};
#if defined(USB_CHARGER)
  StaticIcon* batteryChargeIcon = nullptr;
#endif
#if defined(INTERNAL_MODULE_PXX1) && defined(EXTERNAL_ANTENNA)
  StaticIcon* extAntenna = nullptr;
#endif

  coord_t batteryShellY() const
  {
    return height() - W_BATT_HUD_H - PAD_TINY;
  }

  coord_t rssiClusterWidth() const
  {
    return 4 * rssiBarStep() + rssiBarWidth();
  }

  void layoutStatus()
  {
    if (batteryShell) {
      lv_obj_set_pos(batteryShell, W_BATT_HUD_X, batteryShellY());
      lv_obj_set_size(batteryShell, W_BATT_HUD_W, W_BATT_HUD_H);
    }
    if (batteryCap) {
      lv_obj_set_pos(batteryCap, W_BATT_HUD_X + W_BATT_HUD_W,
                     batteryShellY() + 3);
      lv_obj_set_size(batteryCap, 3, W_BATT_HUD_H - 6);
    }
    if (batteryFill) {
      lv_obj_set_pos(batteryFill, batteryFillX(), batteryFillY());
      lv_obj_set_size(batteryFill, batteryFillWidth(), batteryFillHeight());
      lastBatt = 255;
    }

    coord_t rssiBh = rssiBarHeight();
    const uint8_t rssiBarsHeight[] = {
      (uint8_t)((rssiBh * 5 + 15) / 31),
      (uint8_t)((rssiBh * 10 + 15) / 31),
      (uint8_t)((rssiBh * 15 + 15) / 31),
      (uint8_t)((rssiBh * 21 + 15) / 31),
      (uint8_t)rssiBh};
    for (unsigned int i = 0; i < DIM(rssiBars); i++) {
      if (!rssiBars[i]) continue;
      uint8_t height = rssiBarsHeight[i];
      lv_obj_set_pos(rssiBars[i], rssiX() + i * rssiBarStep(),
                     rssiBarBottom() - height);
      lv_obj_set_size(rssiBars[i], rssiBarWidth(), height);
    }
  }
};

const WidgetOption RadioInfoWidget::options[] = {
    {STR_LOW_BATT_COLOR, WidgetOption::Color, RGB2FLAGS(0xF4, 0x43, 0x36)},
    {STR_MID_BATT_COLOR, WidgetOption::Color, RGB2FLAGS(0xFF, 0xC1, 0x07)},
    {STR_HIGH_BATT_COLOR, WidgetOption::Color, RGB2FLAGS(0x4C, 0xAF, 0x50)},
    {nullptr, WidgetOption::Bool}};

BaseWidgetFactory<RadioInfoWidget> RadioInfoWidget("Radio Info", RadioInfoWidget::options,
                                                   STR_RADIO_INFO_WIDGET);

class DateTimeWidget : public Widget
{
 public:
  DateTimeWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
                 int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    coord_t x = isCompactTopBarWidget()
                    ? TOPBAR_CONTENT_PAD
                    : rect.w - HeaderDateTime::HDR_DATE_WIDTH - DT_XO;
    coord_t dateTimeHeight =
        HeaderDateTime::HDR_DATE_LINE2 + HeaderDateTime::HDR_DATE_HEIGHT + 2;
    coord_t y = isCompactTopBarWidget()
                    ? maxCoord((rect.h - dateTimeHeight) / 2, (coord_t)0)
                    : PAD_THREE;
    dateTime = new (std::nothrow) HeaderDateTime(this, x, y);
    update();
  }

  void foreground() override
  {
    if (_deleted) return;

    Widget::checkEvents();
  }

  void update() override
  {
    if (_deleted) return;

    auto widgetData = getPersistentData();

    // get color from options
    uint32_t color;
    memcpy(&color, &widgetData->options[0].value.unsignedValue, sizeof(color));
    if (!dateTime) return;
    dateTime->setColor(color);
    bool compact = isCompactTopBarWidget();
    coord_t pad = TOPBAR_CONTENT_PAD;
    coord_t displayWidth =
        compact ? maxCoord((coord_t)(width() - 2 * pad), (coord_t)1)
                : HeaderDateTime::HDR_DATE_WIDTH;
    coord_t x = compact ? pad : width() - displayWidth - DT_XO;
    coord_t y = compact ? maxCoord((height() - dateTime->height()) / 2,
                                   (coord_t)0)
                        : PAD_THREE;
    dateTime->setDisplayWidth(displayWidth);
    dateTime->setTextAlign(LV_TEXT_ALIGN_LEFT);
    dateTime->setPos(x, y);
  }

  HeaderDateTime* dateTime = nullptr;
  int8_t lastMinute = -1;

  static const WidgetOption options[];

  // Adjustment to make main view date/time align with model/radio settings views
  static LAYOUT_VAL_SCALED(DT_XO, 1)
};

const WidgetOption DateTimeWidget::options[] = {
    {STR_COLOR, WidgetOption::Color, COLOR2FLAGS(COLOR_THEME_PRIMARY2_INDEX)},
    {nullptr, WidgetOption::Bool}};

BaseWidgetFactory<DateTimeWidget> DateTimeWidget("Date Time",
                                                 DateTimeWidget::options,
                                                 STR_DATE_TIME_WIDGET);

#if defined(INTERNAL_GPS)

class InternalGPSWidget : public Widget
{
 public:
  InternalGPSWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
                    int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    icon =
        new (std::nothrow) StaticIcon(this, width() / 2 - PAD_LARGE - PAD_TINY, ICON_H,
                                      ICON_TOPMENU_GPS, COLOR_THEME_SECONDARY2_INDEX);

    numSats = new (std::nothrow) DynamicNumber<uint16_t>(
        this, {0, 1, width(), SATS_H}, [=] { return gpsData.numSat; },
        COLOR_THEME_PRIMARY2_INDEX, CENTERED | FONT(XS));
  }

  void foreground() override
  {
    if (_deleted) return;

    bool hasGPS = serialGetModePort(UART_MODE_GPS) >= 0;

    if (numSats) numSats->show(hasGPS && (gpsData.numSat > 0));
    if (icon) icon->show(hasGPS);

    if (icon) {
      if (gpsData.fix)
        icon->setColor(COLOR_THEME_PRIMARY2_INDEX);
      else
        icon->setColor(COLOR_THEME_SECONDARY2_INDEX);
    }
  }

 protected:
  StaticIcon* icon = nullptr;
  DynamicNumber<uint16_t>* numSats = nullptr;

  static LAYOUT_VAL_SCALED(ICON_H, 19)
  static LAYOUT_VAL_SCALED(SATS_H, 12)
};

BaseWidgetFactory<InternalGPSWidget> InternalGPSWidget("Internal GPS", nullptr,
                                                       STR_INT_GPS_LABEL);

#endif
