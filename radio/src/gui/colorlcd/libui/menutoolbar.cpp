/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   libopenui - https://github.com/opentx/libopenui
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

#include "menutoolbar.h"

#include "keys.h"
#include "mainwindow.h"
#include "menu.h"
#include "etx_lv_theme.h"
#include "translations/translations.h"

#include <new>

#if defined(SIMU)
static bool forceMenuToolbarLabelCreateFailure = false;

static void menuToolbarForceLabelCreateFailureForTest(bool force)
{
  forceMenuToolbarLabelCreateFailure = force;
}
#endif

static lv_obj_t* menu_toolbar_label_create(lv_obj_t* parent)
{
#if defined(SIMU)
  if (forceMenuToolbarLabelCreateFailure) return nullptr;
#endif
  return etx_label_create(parent);
}

static const lv_obj_class_t menu_button_class = {
    .base_class = &button_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_FALSE,
    .instance_size = sizeof(lv_btn_t),
};

static lv_obj_t* menu_button_create(lv_obj_t* parent)
{
  return etx_create(&menu_button_class, parent);
}

static void toolbar_btn_defocus(lv_event_t* event)
{
  auto btn = static_cast<MenuToolbarButton*>(
      Window::fromAvailableLvObj(lv_event_get_target(event)));
  if (btn) btn->check(false);
}

MenuToolbarButton::MenuToolbarButton(Window* parent, const rect_t& rect,
                                     const char* picto) :
    ButtonBase(parent, rect, nullptr, menu_button_create)
{
  withLive([](LiveWindow& live) {
    lv_obj_add_flag(live.lvobj(), LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  });

  withLive([](LiveWindow& live) {
    lv_obj_add_event_cb(live.lvobj(), toolbar_btn_defocus, LV_EVENT_DEFOCUSED,
                        nullptr);
  });

  lv_obj_t* label = nullptr;
  initRequiredLvObj(label, menu_toolbar_label_create, [&](lv_obj_t* obj) {
    lv_label_set_text(obj, picto);
    lv_obj_center(obj);
  });
}

MenuToolbar::MenuToolbar(Choice& choice, Menu& menu, const int columns) :
    Window(&menu, {0, 0, 0, 0}),
    choice(choice),
    menu(menu),
    filterColumns(columns),
    group(lv_group_create())
{
  setWindowFlag(OPAQUE);
  if (filterColumns <= 0) {
    failClosed();
    return;
  }
  if (!requireLvGroup(group)) return;

  withLive([&](LiveWindow& live) {
    padAll(PAD_SMALL);

    auto obj = live.lvobj();
    etx_solid_bg(obj);
    etx_obj_add_style(obj, styles->outline, LV_PART_MAIN);
    etx_obj_add_style(obj, styles->outline_color_normal, LV_PART_MAIN);

    setWidth((MENUS_TOOLBAR_BUTTON_WIDTH + PAD_SMALL) * columns + PAD_SMALL);

    addButton(STR_SELECT_MENU_ALL, choice.getMin(), choice.getMax(), nullptr,
              nullptr, true);

    changeFilterMsg.subscribe(Messaging::MENU_CHANGE_FILTER, [=](uint32_t param) {
      if (param == 1)
        nextFilter();
      else
        prevFilter();
    });
  });
}

MenuToolbar::~MenuToolbar()
{
  if (group) lv_group_del(group);
}

void MenuToolbar::resetFilter()
{
  withLive([&](LiveWindow& live) {
    auto obj = live.lvobj();
    if (lv_group_get_focused(group) != obj) {
      lv_group_focus_obj(obj);
      choice.fillMenu(&menu);
      menu.setTitle(choice.getTitle());
    }
  });
}

void MenuToolbar::nextFilter()
{
  withLive([&](LiveWindow&) {
    lv_group_focus_next(group);
    if (auto window = Window::fromAvailableLvObj(lv_group_get_focused(group)))
      window->sendLvEvent(LV_EVENT_CLICKED);
  });
}

void MenuToolbar::prevFilter()
{
  withLive([&](LiveWindow&) {
    lv_group_focus_prev(group);
    if (auto window = Window::fromAvailableLvObj(lv_group_get_focused(group)))
      window->sendLvEvent(LV_EVENT_CLICKED);
  });
}

rect_t MenuToolbar::getButtonRect(bool wideButton)
{
  if (wideButton && (nxtBtnPos % filterColumns))
    nxtBtnPos = nxtBtnPos - (nxtBtnPos % filterColumns) + filterColumns;
  coord_t x =
      (nxtBtnPos % filterColumns) * (MENUS_TOOLBAR_BUTTON_WIDTH + PAD_SMALL);
  coord_t y =
      (nxtBtnPos / filterColumns) * (EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_SMALL);
  coord_t w = wideButton ? (MENUS_TOOLBAR_BUTTON_WIDTH + PAD_SMALL) *
                                   (filterColumns - 1) +
                               MENUS_TOOLBAR_BUTTON_WIDTH
                         : MENUS_TOOLBAR_BUTTON_WIDTH;
  nxtBtnPos += wideButton ? filterColumns : 1;
  return {x, y, w, EdgeTxStyles::UI_ELEMENT_HEIGHT};
}

bool MenuToolbar::filterMenu(MenuToolbarButton* btn, int16_t filtermin,
                             int16_t filtermax,
                             const Choice::FilterFct& filterFunc,
                             const char* title)
{
  bool checked = false;
  withLive([&](LiveWindow&) {
    if (!btn) return;
    btn->withLive([&](Window::LiveWindow& btnLive) {
      btn->check(!btn->checked());

      filter = nullptr;
      if (btn->checked()) {
        if (title)
          menu.setTitle(title);
        else
          menu.setTitle(choice.getTitle());
        filter = [=](int16_t index) {
          if (filterFunc) return filterFunc(index);
          return index == 0 || (abs(index) >= filtermin && abs(index) <= filtermax);
        };
        lv_group_focus_obj(btnLive.lvobj());
        choice.fillMenu(&menu, filter);
      } else {
        if (allBtn) allBtn->sendLvEvent(LV_EVENT_CLICKED);
      }

      checked = btn->checked();
    });
  });

  return checked;
}

void MenuToolbar::addButton(const char* picto, int16_t filtermin,
                            int16_t filtermax,
                            const Choice::FilterFct& filterFunc,
                            const char* title, bool wideButton)
{
  withLive([&](LiveWindow&) {
    int vmin = choice.getMin();
    int vmax = choice.getMax();

    if (vmin > filtermin || vmax < filtermin) return;

    if (choice.isValueAvailable) {
      bool found = false;
      for (int i = filtermin; i <= filtermax; i += 1) {
        if (choice.isValueAvailable(i)) {
          if (filterFunc && !filterFunc(i))
            continue;
          found = true;
          break;
        }
      }
      if (!found) return;
    }

    rect_t r = getButtonRect(wideButton);
    buildRequiredWindow<MenuToolbarButton>(
        [&](MenuToolbarButton& button) {
          button.setPressHandler(std::bind(&MenuToolbar::filterMenu, this,
                                           &button, filtermin, filtermax,
                                           filterFunc, title));

          button.withLive([&](Window::LiveWindow& liveButton) {
            lv_group_add_obj(group, liveButton.lvobj());
          });

          if (children.size() == 1) {
            allBtn = &button;
            allBtn->sendLvEvent(LV_EVENT_CLICKED);
          }
        },
        this, r, picto);
  });
}

#if defined(SIMU)
bool menuToolbarButtonLabelAllocationFailureFailsClosedForTest()
{
  menuToolbarForceLabelCreateFailureForTest(true);
  auto button = new (std::nothrow) MenuToolbarButton(
      MainWindow::instance(), {0, 0, 36, EdgeTxStyles::UI_ELEMENT_HEIGHT},
      "A");
  menuToolbarForceLabelCreateFailureForTest(false);

  if (!button) return false;
  button->onClicked();

  bool ok = !button->isAvailable() && !button->isVisible() &&
            !button->automationClickable();
  delete button;
  return ok;
}
#endif
