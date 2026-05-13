/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "dialogs/battery_confirm_dialog.h"

#include "button.h"
#include "fonts.h"
#include "static.h"
#include "edgetx.h"
#include "telemetry/battery_monitor.h"

BatteryConfirmDialog::BatteryConfirmDialog(uint8_t monitor, uint16_t packMask)
    : FullScreenDialog(WARNING_TYPE_ASTERISK, "Battery connected", "",
                       "", nullptr),
      monitor(monitor), packMask(packMask)
{
  markFlightBatteryPromptShown(monitor);
  buildBody();
}

void BatteryConfirmDialog::buildBody()
{
  auto div = Window::makeLive<Window>(
      this, rect_t{0, ALERT_FRAME_TOP, LCD_W, ALERT_FRAME_HEIGHT});
  if (div) {
    div->setWindowFlag(NO_FOCUS);
    div->withLive([](Window::LiveWindow& live) {
      etx_solid_bg(live.lvobj(), COLOR_THEME_PRIMARY2_INDEX);
    });
  }

  Window::makeLive<StaticText>(
      this,
      rect_t{ALERT_TITLE_LEFT, ALERT_TITLE_TOP,
             LCD_W - ALERT_TITLE_LEFT - PAD_MEDIUM,
             LCD_H - ALERT_TITLE_TOP - PAD_MEDIUM},
      "Battery connected", COLOR_THEME_WARNING_INDEX, FONT(XL));

  auto body = Window::makeLive<Window>(
      this, rect_t{0, ALERT_MESSAGE_TOP, LCD_W,
                   LCD_H - ALERT_MESSAGE_TOP - PAD_LARGE - EdgeTxStyles::UI_ELEMENT_HEIGHT - PAD_LARGE});
  if (!body) return;

  body->setFlexLayout(LV_FLEX_FLOW_COLUMN_WRAP, PAD_TINY);

  bool specSeen[MAX_BATTERY_PACKS] = {};
  for (uint8_t slot = 0; slot < MAX_BATTERY_PACKS; slot++) {
    if (specSeen[slot] || !(packMask & (1 << slot))) continue;

    BatteryPackData* pack = &g_eeGeneral.batteryPacks[slot];
    if (!pack->active) continue;

    for (uint8_t j = slot + 1; j < MAX_BATTERY_PACKS; j++) {
      if ((packMask & (1 << j)) && g_eeGeneral.batteryPacks[j].active &&
          batterySpecEquals(*pack, g_eeGeneral.batteryPacks[j]))
        specSeen[j] = true;
    }

    char label[32];
    snprintf(label, sizeof(label), "%s %dS %dmAh",
             batteryTypeToString((BatteryType)pack->batteryType),
             pack->cellCount, pack->capacity);

    auto btn = Window::makeLive<TextButton>(
        body, rect_t{}, label, [=]() -> uint8_t {
          onConfirmPack(slot + 1);
          return 0;
        });
    if (btn) {
      btn->setFont(FONT_BOLD_INDEX);
      btn->setWrap();
    }
  }

  if (flightBatteryPromptAllowsManual(monitor)) {
    auto btn = Window::makeLive<TextButton>(
        body, rect_t{}, "Use model setting", [=]() -> uint8_t {
          onConfirmPack(0);
          return 0;
        });
    if (btn) {
      btn->setFont(FONT_BOLD_INDEX);
      btn->setWrap();
    }
  }
}

void BatteryConfirmDialog::onConfirmPack(uint8_t slot)
{
  if (flightBatterySessionState(monitor) != FlightBatterySessionState::NeedsConfirmation) {
    closeDialog();
    return;
  }
  if (!confirmFlightBatteryPack(monitor, slot)) {
    return;
  }
  closeDialog();
}

void BatteryConfirmDialog::closeDialog()
{
  deleteLater();
}
