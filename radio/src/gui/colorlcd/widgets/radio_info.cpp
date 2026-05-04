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
                    ? 0
                    : rect.w - HeaderDateTime::HDR_DATE_WIDTH - DT_XO;
    dateTime = new (std::nothrow) HeaderDateTime(this, x, PAD_THREE);
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
    coord_t displayWidth = compact ? width() : HeaderDateTime::HDR_DATE_WIDTH;
    coord_t x = compact ? 0 : width() - displayWidth - DT_XO;
    dateTime->setDisplayWidth(displayWidth);
    dateTime->setPos(x, PAD_THREE);
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
