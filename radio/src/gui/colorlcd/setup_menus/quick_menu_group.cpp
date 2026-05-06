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

#include "quick_menu_group.h"

#include <new>

#include "bitmaps.h"
#include "button.h"
#include "quick_menu_def.h"
#include "static.h"

static void etx_quick_button_constructor(const lv_obj_class_t* class_p,
                                         lv_obj_t* obj)
{
  etx_obj_add_style(obj, styles->rounded, LV_PART_MAIN);
  etx_txt_color(obj, COLOR_THEME_QM_FG_INDEX, LV_PART_MAIN);
  etx_obj_add_style(obj, styles->pad_zero, LV_PART_MAIN);
  etx_solid_bg(obj, COLOR_THEME_QM_BG_INDEX, LV_PART_MAIN);

  etx_solid_bg(obj, COLOR_THEME_QM_FG_INDEX, LV_PART_MAIN | LV_STATE_FOCUSED);
}

static const lv_obj_class_t etx_quick_button_class = {
    .base_class = &lv_btn_class,
    .constructor_cb = etx_quick_button_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = QuickMenuGroup::QM_BUTTON_WIDTH,
    .height_def = QuickMenuGroup::QM_BUTTON_HEIGHT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_TRUE,
    .instance_size = sizeof(lv_btn_t),
};

static lv_obj_t* etx_quick_button_create(lv_obj_t* parent)
{
  return etx_create(&etx_quick_button_class, parent);
}

class QuickMenuButton : public ButtonBase
{
 public:
  QuickMenuButton(Window* parent, EdgeTxIcon icon, const char* title,
                  std::function<uint8_t(void)> pressHandler,
                  std::function<bool(void)> visibleHandler) :
      ButtonBase(parent, {}, pressHandler, etx_quick_button_create),
      visibleHandler(std::move(visibleHandler))
  {
#if defined(SIMU)
    setAutomationText(title ? title : "");
#endif

    iconPtr = Window::makeLive<StaticIcon>(
        this,
        (QuickMenuGroup::QM_BUTTON_WIDTH - QuickMenuGroup::QM_ICON_SIZE) / 2,
        PAD_SMALL, icon, COLOR_THEME_QM_FG_INDEX);
    if (!iconPtr) {
      failClosed();
      return;
    }
#if VERSION_MAJOR > 2
    iconPtr->withLive([](Window::LiveWindow& liveIcon) {
      etx_obj_add_style(liveIcon.lvobj(), styles->qmdisabled,
                        LV_PART_MAIN | LV_STATE_DISABLED);
    });
#endif
    iconPtr->withLive([](Window::LiveWindow& liveIcon) {
      etx_img_color(liveIcon.lvobj(), COLOR_THEME_QM_BG_INDEX, LV_STATE_USER_1);
    });

    textPtr = Window::makeLive<StaticText>(
        this,
        rect_t{0, QuickMenuGroup::QM_ICON_SIZE + PAD_TINY * 2,
               QuickMenuGroup::QM_BUTTON_WIDTH - 1, 0},
        title, COLOR_THEME_QM_FG_INDEX, CENTERED | FONT(XS));
    if (!textPtr) {
      failClosed();
      return;
    }
#if VERSION_MAJOR > 2
    textPtr->withLive([](Window::LiveWindow& liveText) {
      etx_obj_add_style(liveText.lvobj(), styles->qmdisabled,
                        LV_PART_MAIN | LV_STATE_DISABLED);
    });
#endif
    textPtr->withLive([](Window::LiveWindow& liveText) {
      etx_txt_color(liveText.lvobj(), COLOR_THEME_QM_BG_INDEX, LV_STATE_USER_1);
    });

    withLive([](LiveWindow& live) {
      lv_obj_add_event_cb(live.lvobj(), QuickMenuButton::focused_cb,
                          LV_EVENT_FOCUSED, nullptr);
      lv_obj_add_event_cb(live.lvobj(), QuickMenuButton::defocused_cb,
                          LV_EVENT_DEFOCUSED, nullptr);
    });
  }

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "QuickMenuButton"; }
#endif

  static void focused_cb(lv_event_t* e)
  {
    QuickMenuButton* b =
        (QuickMenuButton*)lv_obj_get_user_data(lv_event_get_target(e));
    if (b) b->setFocused();
  }

  static void defocused_cb(lv_event_t* e)
  {
    QuickMenuButton* b =
        (QuickMenuButton*)lv_obj_get_user_data(lv_event_get_target(e));
    if (b) b->setDeFocused();
  }

  void setDisabled()
  {
    if (iconPtr) iconPtr->enable(false);
    if (textPtr) textPtr->enable(false);
  }

  void setEnabled()
  {
    if (iconPtr) iconPtr->enable(true);
    if (textPtr) textPtr->enable(true);
  }

  void setFocused()
  {
    if (textPtr)
      textPtr->withLive([](Window::LiveWindow& live) {
        lv_obj_add_state(live.lvobj(), LV_STATE_USER_1);
      });
    if (iconPtr)
      iconPtr->withLive([](Window::LiveWindow& live) {
        lv_obj_add_state(live.lvobj(), LV_STATE_USER_1);
      });
  }

  void setDeFocused()
  {
    if (textPtr)
      textPtr->withLive([](Window::LiveWindow& live) {
        lv_obj_clear_state(live.lvobj(), LV_STATE_USER_1);
      });
    if (iconPtr)
      iconPtr->withLive([](Window::LiveWindow& live) {
        lv_obj_clear_state(live.lvobj(), LV_STATE_USER_1);
      });
  }

  bool isVisible()
  {
    if (visibleHandler) return visibleHandler();
    return true;
  }

  void onLiveShow(LiveWindow& live, bool vis) override
  {
    ButtonBase::onLiveShow(live, vis && isVisible());
  }

 protected:
  StaticIcon* iconPtr = nullptr;
  StaticText* textPtr = nullptr;
  std::function<bool(void)> visibleHandler = nullptr;
};

QuickMenuGroup::QuickMenuGroup(Window* parent) :
    Window(parent, {0, 0, parent->width(), parent->height()})
{
  padAll(PAD_OUTLINE);
  group = lv_group_create();
}

ButtonBase* QuickMenuGroup::addButton(EdgeTxIcon icon, const char* title,
                                      std::function<void(void)> pressHandler,
                                      std::function<bool(void)> visibleHandler,
                                      std::function<void(bool)> focusHandler)
{
  ButtonBase* b = Window::makeLive<QuickMenuButton>(
      this, icon, title,
      [=]() {
        pressHandler();
        return 0;
      },
      visibleHandler);
  if (!b) return nullptr;
  b->setLongPressHandler([=]() {
    pressHandler();
    return 0;
  });
  btns.push_back(b);
  if (group)
    b->withLive([&](Window::LiveWindow& live) {
      lv_group_add_obj(group, live.lvobj());
    });
  b->setFocusHandler([=](bool focus) {
    if (focus) curBtn = b;
    if (focusHandler) focusHandler(focus);
  });
  if (curBtn == nullptr) curBtn = b;
  return b;
}

void QuickMenuGroup::setGroup()
{
  if (group && group != lv_group_get_default()) {
    lv_group_set_default(group);

    lv_indev_t* indev = lv_indev_get_next(NULL);
    while (indev) {
      lv_indev_set_group(indev, group);
      indev = lv_indev_get_next(indev);
    }
  }
}

void QuickMenuGroup::onDelete()
{
  if (group) lv_group_del(group);
  group = nullptr;
}

void QuickMenuGroup::setFocus()
{
  if (!curBtn && btns.size() > 0) curBtn = btns[0];

  if (curBtn) {
    curBtn->sendLvEvent(LV_EVENT_FOCUSED);
    curBtn->focus();
  }
}

void QuickMenuGroup::clearFocus()
{
  if (curBtn) {
    ((QuickMenuButton*)curBtn)->setEnabled();
    curBtn->sendLvEvent(LV_EVENT_DEFOCUSED);
  }
}

void QuickMenuGroup::setDisabled(bool all)
{
  for (size_t i = 0; i < btns.size(); i += 1) {
    if (btns[i] != curBtn || all) {
      ((QuickMenuButton*)btns[i])->setDisabled();
      btns[i]->sendLvEvent(LV_EVENT_DEFOCUSED);
    }
  }
}

void QuickMenuGroup::setEnabled()
{
  for (size_t i = 0; i < btns.size(); i += 1) {
    ((QuickMenuButton*)btns[i])->setEnabled();
  }
}

void QuickMenuGroup::setCurrent(ButtonBase* b)
{
  curBtn = b;
  ((QuickMenuButton*)b)->setEnabled();
}

void QuickMenuGroup::activate()
{
  setFocus();
  setGroup();
  setEnabled();
  show();
}

void QuickMenuGroup::doLayout(int cols)
{
  int n = 0;
  for (size_t i = 0; i < btns.size(); i += 1) {
    if (((QuickMenuButton*)btns[i])->isVisible()) {
      coord_t x = (n % cols) * (QM_BUTTON_WIDTH + PAD_MEDIUM);
      coord_t y = (n / cols) * (QM_BUTTON_HEIGHT + PAD_MEDIUM);
      btns[i]->setPos(x, y);
      n += 1;
    }
    btns[i]->show();
  }
}

void QuickMenuGroup::nextEntry()
{
  if (group) lv_group_focus_next(group);
}

void QuickMenuGroup::prevEntry()
{
  if (group) lv_group_focus_prev(group);
}

ButtonBase* QuickMenuGroup::getFocusedButton()
{
  if (group) {
    lv_obj_t* b = lv_group_get_focused(group);
    if (b) {
      for (size_t i = 0; i < btns.size(); i += 1)
        if (btns[i]->withLive(
                [&](Window::LiveWindow& live) { return live.lvobj() == b; }))
          return btns[i];
    }
  }
  return nullptr;
}
