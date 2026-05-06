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

#include "widgets_setup.h"

#include "menu.h"
#include "myeeprom.h"
#include "storage/storage.h"
#include "etx_lv_theme.h"
#include "view_main.h"
#include "widget_settings.h"
#include "pagegroup.h"
#include "screen_setup.h"
#include "topbar.h"

#include <new>

SetupWidgetsPageSlot::SetupWidgetsPageSlot(Window* parent, const rect_t& rect,
                                           WidgetsContainer* container,
                                           uint8_t slotIndex,
                                           SetupTopBarWidgetsPage* topBarSetupPage) :
    ButtonBase(parent, rect),
    container(container),
    topBarSetupPage(topBarSetupPage),
    slot(WidgetSlotIndex{slotIndex})
{
  setPressHandler([this]() -> uint8_t {
    if (!this->container) return 0;
    if (this->container->getWidget(this->slot.asUnsigned())) {
      Menu* menu = new (std::nothrow) Menu();
      if (!menu) return 0;
      menu->addLine(STR_SELECT_WIDGET,
                    [this]() { addNewWidget(); });
      auto widget = this->container->getWidget(this->slot.asUnsigned());
      if (widget->hasOptions())
        menu->addLine(STR_WIDGET_SETTINGS,
                      [=]() { new (std::nothrow) WidgetSettings(widget); });
      if (this->container->canMoveWidget(this->slot, WidgetMoveDirection::Left))
        menu->addLine(STR_MOVE_LEFT,
                      [this]() { moveWidget(WidgetMoveDirection::Left); });
      if (this->container->canMoveWidget(this->slot, WidgetMoveDirection::Right))
        menu->addLine(STR_MOVE_RIGHT,
                      [this]() { moveWidget(WidgetMoveDirection::Right); });
      menu->addLine(STR_REMOVE_WIDGET,
                    [this]() {
                      this->container->removeWidget(this->slot.asUnsigned());
                    });
    } else {
      addNewWidget();
    }

    return 0;
  });

  etx_obj_add_style(lvobj, styles->border, LV_STATE_FOCUSED);
  etx_obj_add_style(lvobj, styles->border_color[COLOR_THEME_PRIMARY1_INDEX],
                    LV_STATE_FOCUSED);
  etx_obj_add_style(lvobj, styles->state_focus_frame, LV_STATE_FOCUSED);

  lv_style_init(&borderStyle);
  lv_style_set_line_width(&borderStyle, PAD_BORDER);
  lv_style_set_line_opa(&borderStyle, LV_OPA_COVER);
  lv_style_set_line_dash_width(&borderStyle, PAD_BORDER);
  lv_style_set_line_dash_gap(&borderStyle, PAD_BORDER);
  lv_style_set_line_color(&borderStyle, makeLvColor(COLOR_THEME_SECONDARY2));

  border = lv_line_create(lvobj);
  if (border) {
    lv_obj_add_style(border, &borderStyle, LV_PART_MAIN);
  }

  updateBorder();
  setFocusState();

  setFocusHandler([=](bool) { setFocusState(); });
}

void SetupWidgetsPageSlot::setSlotRect(const rect_t& rect)
{
  setRect(rect);
  updateBorder();
}

void SetupWidgetsPageSlot::updateBorder()
{
  borderPts[0] = {1, 1};
  borderPts[1] = {(lv_coord_t)(width() - 1), 1};
  borderPts[2] = {(lv_coord_t)(width() - 1), (lv_coord_t)(height() - 1)};
  borderPts[3] = {1, (lv_coord_t)(height() - 1)};
  borderPts[4] = {1, 1};

  if (border) lv_line_set_points(border, borderPts, 5);
}

void SetupWidgetsPageSlot::setFocusState()
{
  if (!border) return;
  if (hasFocus()) {
    lv_obj_add_flag(border, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(border, LV_OBJ_FLAG_HIDDEN);
  }
}

void SetupWidgetsPageSlot::addNewWidget()
{
  if (!container) return;
  const char* cur = nullptr;
  auto w = container->getWidget(slot.asUnsigned());
  if (w) cur = w->getFactory()->getDisplayName();

  Menu* menu = new (std::nothrow) Menu();
  if (!menu) return;
  menu->setTitle(STR_SELECT_WIDGET);
  int selected = -1;
  int index = 0;
  for (const auto& registered : WidgetFactory::getRegisteredWidgets()) {
    auto factory = &registered.get();
    if (strcmp(factory->getName(), "Radio Info") == 0) continue;
    auto selectedSlot = slot;
    auto selectedContainer = container;
    menu->addLine(factory->getDisplayName(), [=]() {
      selectedContainer->createWidget(selectedSlot.asUnsigned(), factory);
      auto widget = selectedContainer->getWidget(selectedSlot.asUnsigned());
      if (widget && widget->hasOptions())
        new (std::nothrow) WidgetSettings(widget);
    });
    if (cur && strcmp(cur, factory->getDisplayName()) == 0)
      selected = index;
    index += 1;
  }

  if (selected >= 0)
    menu->select(selected);
}

void SetupWidgetsPageSlot::moveWidget(WidgetMoveDirection direction)
{
  if (!container) return;

  WidgetMoveResult moveResult = container->moveWidget(slot, direction);
  if (topBarSetupPage)
    topBarSetupPage->refreshSlots(moveResult);
}

SetupWidgetsPage::SetupWidgetsPage(uint8_t customScreenIdx) :
    NavWindow(ViewMain::instance(), rect_t{}), customScreenIdx(customScreenIdx)
{
  pushLayer();

  // attach this custom screen here so we can display it
  auto screen = customScreens[customScreenIdx];
  if (screen) {
    setRect(screen->getRect());
    auto viewMain = ViewMain::instance();
    if (viewMain) {
      savedView = viewMain->getCurrentMainView();
      viewMain->setCurrentMainView(customScreenIdx);
      if (!viewMain->hasTopbar()) viewMain->hideTopBarEdgeTxButton();
    }
  }

  if (!screen) return;
  SetupWidgetsPageSlot* firstSlot = nullptr;
  for (unsigned i = 0; i < screen->getZonesCount(); i++) {
    auto rect = screen->getZone(i);
    auto widget_container = customScreens[customScreenIdx];
    auto slot = new (std::nothrow) SetupWidgetsPageSlot(this, rect, widget_container, i);
    if (i == 0) firstSlot = slot;
  }
  if (firstSlot) lv_group_focus_obj(firstSlot->getLvObj());

#if defined(HARDWARE_TOUCH)
  addBackButton();
#endif

  screen->show();
}

void SetupWidgetsPage::onLiveClicked(Window::LiveWindow&)
{
  // block event forwarding (window is transparent)
}

void SetupWidgetsPage::onCancel()
{
  deleteLater();
  QuickMenu::openPage((QMPage)(QM_UI_SCREEN1 + customScreenIdx));
}

void SetupWidgetsPage::onDeleted()
{
  // and continue async deletion...
  auto screen = customScreens[customScreenIdx];
  if (screen) {
    auto viewMain = ViewMain::instance();
    viewMain->setCurrentMainView(savedView);
    viewMain->showTopBarEdgeTxButton();
  }

  storageDirty(EE_MODEL);
}
