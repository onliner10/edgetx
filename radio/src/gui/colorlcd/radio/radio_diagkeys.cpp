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

#include "radio_diagkeys.h"

#include "hal/rotary_encoder.h"
#include "edgetx.h"

#if defined(RADIO_PL18U)
  static const uint8_t _trimMap[MAX_TRIMS * 2] = {6, 7, 4, 5, 2, 3, 0, 1,
                                                  10, 11, 8, 9, 12, 13, 14, 15};
#elif defined(RADIO_NB4P)
  static const uint8_t _trimMap[MAX_TRIMS * 2] = {0, 1, 2, 3, 4, 5, 6, 7,
                                                  8, 9, 10, 11, 12, 13, 14, 15};
#elif defined(PCBPL18)
  static const uint8_t _trimMap[MAX_TRIMS * 2] = {8, 9, 10, 11, 12, 13, 14, 15,
                                                  2, 3, 4,  5,  0,  1,  6,  7};
#else
  static const uint8_t _trimMap[MAX_TRIMS * 2] = {6, 7, 4, 5, 2,  3,
                                                  0, 1, 8, 9, 10, 11};
#endif

static EnumKeys get_ith_key(uint8_t i)
{
  auto supported_keys = keysGetSupported();
  for (uint8_t k = 0; k < MAX_KEYS; k++) {
    if (supported_keys & (1 << k)) {
      if (i-- == 0) return (EnumKeys)k;
    }
  }

  // should not get here,
  // we assume: i < keysGetMaxKeys()
  return (EnumKeys)0;
}

static lv_obj_t* createDiagLabel(lv_obj_t* parent, const char* text, coord_t x,
                                 coord_t y)
{
  auto label = etx_label_create(parent);
  if (label) {
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
  }
  return label;
}

static void setDiagLabelText(lv_obj_t* label, const char* text)
{
  if (label) lv_label_set_text(label, text);
}

class RadioKeyDiagsWindow : public Window
{
 public:
  RadioKeyDiagsWindow(Window *parent, const rect_t &rect) : Window(parent, rect)
  {
    padAll(PAD_ZERO);

    coord_t colWidth = (width() - PAD_LARGE * 3) / 3;
    coord_t colHeight = height() - PAD_LARGE - PAD_SMALL;

    Window* form;
    coord_t x = PAD_MEDIUM;

    if (keysGetMaxKeys() > 0) {
      form = new Window(parent, rect_t{x, PAD_MEDIUM, colWidth, colHeight});
      form->textColor(COLOR_THEME_PRIMARY1_INDEX);
      addKeys(form);
      x += colWidth + PAD_MEDIUM;
    } else {
      colWidth = (width() - PAD_MEDIUM * 3) / 2;
    }

    form = new Window(parent, rect_t{x, PAD_MEDIUM, colWidth, colHeight});
    form->textColor(COLOR_THEME_PRIMARY1_INDEX);
    addSwitches(form);
    x += colWidth + PAD_MEDIUM;

    form = new Window(parent, rect_t{x, PAD_MEDIUM, colWidth, colHeight});
    form->textColor(COLOR_THEME_PRIMARY1_INDEX);
    addTrims(form);
  }

  ~RadioKeyDiagsWindow()
  {
    delete keyValues;
    delete switchValues;
    delete trimValues;
  }

  void addKeys(Window *form)
  {
    keyValues = new lv_obj_t *[keysGetMaxKeys()]();

    form->withLive([&](Window::LiveWindow& live) {
      auto obj = live.lvobj();
      uint8_t i;

      // KEYS
      for (i = 0; i < keysGetMaxKeys(); i++) {
        auto k = get_ith_key(i);
        auto y = i * EdgeTxStyles::STD_FONT_HEIGHT;
        createDiagLabel(obj, keysGetLabel(k), 0, y);
        keyValues[i] = createDiagLabel(obj, "", KVAL_X, y);
      }

#if defined(ROTARY_ENCODER_NAVIGATION) && !defined(USE_HATS_AS_KEYS)
      auto y = (i + 1) * EdgeTxStyles::STD_FONT_HEIGHT;
      createDiagLabel(obj, STR_ROTARY_ENCODER, 0, y);
      reValue = createDiagLabel(obj, "", KVAL_X, y);
#endif
    });
  }

  void addSwitches(Window *form)
  {
    switchValues = new lv_obj_t *[switchGetMaxAllSwitches()]();

    form->withLive([&](Window::LiveWindow& live) {
      auto obj = live.lvobj();
      uint8_t row = 0;

      // SWITCHES
      for (uint8_t i = 0; i < switchGetMaxAllSwitches(); i++) {
        if (SWITCH_EXISTS(i) && !switchIsCustomSwitch(i)) {
          switchValues[i] =
              createDiagLabel(obj, "", 0, row * EdgeTxStyles::STD_FONT_HEIGHT);
          row += 1;
        }
      }
    });
  }

  void addTrims(Window *form)
  {
    trimValues = new lv_obj_t *[keysGetMaxTrims() * 2]();
    char s[10];

    form->withLive([&](Window::LiveWindow& live) {
      auto obj = live.lvobj();
      createDiagLabel(obj, STR_TRIMS, 0, 0);
      createDiagLabel(obj, "-", TRIM_MINUS_X, 0);
      createDiagLabel(obj, "+", TRIM_PLUS_X, 0);

      // TRIMS
      for (uint8_t i = 0; i < keysGetMaxTrims(); i++) {
        formatNumberAsString(s, 10, i + 1, 0, 10, "T");
        auto y = i * EdgeTxStyles::STD_FONT_HEIGHT +
                 EdgeTxStyles::STD_FONT_HEIGHT;
        createDiagLabel(obj, s, PAD_SMALL, y);
        trimValues[i * 2] = createDiagLabel(obj, "", TRIM_MINUS_X - PAD_TINY, y);
        trimValues[i * 2 + 1] = createDiagLabel(obj, "", TRIM_PLUS_X, y);
      }
    });
  }

  void setKeyState()
  {
    char s[10] = "0";

    for (uint8_t i = 0; i < keysGetMaxKeys(); i++) {
      auto k = get_ith_key(i);
      s[0] = keysGetState(k) + '0';
      setDiagLabelText(keyValues[i], s);
    }

#if defined(ROTARY_ENCODER_NAVIGATION) && !defined(USE_HATS_AS_KEYS)
    formatNumberAsString(s, 10, rotaryEncoderGetValue());
    setDiagLabelText(reValue, s);
#endif
  }

  void setSwitchState()
  {
    uint8_t i;

    for (i = 0; i < switchGetMaxAllSwitches(); i++) {
      if (SWITCH_EXISTS(i) && !switchIsCustomSwitch(i)) {
        getvalue_t val = getValue(MIXSRC_FIRST_SWITCH + i);
        getvalue_t sw =
            ((val < 0) ? 3 * i + 1 : ((val == 0) ? 3 * i + 2 : 3 * i + 3));
        setDiagLabelText(switchValues[i], getSwitchPositionName(sw));
      }
    }
  }

  void setTrimState()
  {
    char s[10] = "0";

    for (int i = 0; i < keysGetMaxTrims() * 2; i++) {
      s[0] = keysGetTrimState(_trimMap[i]) + '0';
      setDiagLabelText(trimValues[i], s);
    }
  }

  void onLiveCheckEvents(LiveWindow& live) override
  {
    setKeyState();
    setSwitchState();
    setTrimState();
  }

 protected:
  lv_obj_t **keyValues = nullptr;
#if defined(ROTARY_ENCODER_NAVIGATION) && !defined(USE_HATS_AS_KEYS)
  lv_obj_t *reValue = nullptr;
#endif
  lv_obj_t **switchValues = nullptr;
  lv_obj_t **trimValues = nullptr;

  static LAYOUT_VAL_SCALED(KVAL_X, 70)
  static LAYOUT_VAL_SCALED(TRIM_MINUS_X, 62)
  static LAYOUT_VAL_SCALED(TRIM_PLUS_X, 75)
};

void RadioKeyDiagsPage::buildHeader(Window *window)
{
  header->setTitle(STR_HARDWARE);
  header->setTitle2(STR_MENU_RADIO_SWITCHES);
}

void RadioKeyDiagsPage::buildBody(Window *window)
{
  body->padAll(PAD_ZERO);
  new RadioKeyDiagsWindow(window, {0, 0, window->width(), window->height()});
}

RadioKeyDiagsPage::RadioKeyDiagsPage() : Page(ICON_MODEL_SETUP)
{
  buildHeader(header);
  buildBody(body);
}
