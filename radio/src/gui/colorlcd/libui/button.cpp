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

#include "button.h"

#include "static.h"
#include "etx_lv_theme.h"
#include "mainwindow.h"

#include <new>

//-----------------------------------------------------------------------------

static void button_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
  etx_btn_style(obj, LV_PART_MAIN);
}

// Must not be static - inherited by menu_button_class
const lv_obj_class_t button_class = {
    .base_class = &lv_btn_class,
    .constructor_cb = button_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = LV_SIZE_CONTENT,
    .height_def = EdgeTxStyles::UI_ELEMENT_HEIGHT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_TRUE,
    .instance_size = sizeof(lv_btn_t),
};

static lv_obj_t* button_create(lv_obj_t* parent)
{
  return etx_create(&button_class, parent);
}

#if defined(SIMU)
static bool forceTextButtonLabelCreateFailureForTest = false;
#endif

static lv_obj_t* text_button_label_create(lv_obj_t* parent)
{
#if defined(SIMU)
  if (forceTextButtonLabelCreateFailureForTest) return nullptr;
#endif
  return etx_label_create(parent);
}

Button::Button(Window* parent, const rect_t& rect,
               std::function<uint8_t(void)> pressHandler) :
    ButtonBase(parent, rect, pressHandler, button_create)
{
}

//-----------------------------------------------------------------------------

ButtonBase::ButtonBase(Window* parent, const rect_t& rect,
                       std::function<uint8_t(void)> pressHandler,
                       LvglCreate objConstruct) :
    FormField(parent, rect, objConstruct ? objConstruct : lv_btn_create),
    pressHandler(std::move(pressHandler))
{
}

void ButtonBase::check(bool checked)
{
  withAvailableLvObj([&](lv_obj_t* obj) {
    if (checked != this->checked()) {
      if (checked)
        lv_obj_add_state(obj, LV_STATE_CHECKED);
      else
        lv_obj_clear_state(obj, LV_STATE_CHECKED);
    }
  });
}

bool ButtonBase::checked() const
{
  bool result = false;
  withAvailableLvObj([&](lv_obj_t* obj) {
    result = lv_obj_get_state(obj) & LV_STATE_CHECKED;
  });
  return result;
}

void ButtonBase::onPress()
{
  if (!isAvailable() || deleted()) return;
  check(pressHandler && pressHandler());
}

bool ButtonBase::onLongPress()
{
  if (!isAvailable() || deleted()) return false;
  if (longPressHandler) {
    check(longPressHandler());
    withLvObj([](lv_obj_t* obj) {
      lv_obj_clear_state(obj, LV_STATE_PRESSED);
    });
    lv_indev_wait_release(lv_indev_get_act());
    return false;
  }
  return true;
}

void ButtonBase::onClicked() { onPress(); }

void ButtonBase::checkEvents()
{
  Window::checkEvents();
  if (isAvailable() && checkHandler) checkHandler();
}

//-----------------------------------------------------------------------------

TextButton::TextButton(Window* parent, const rect_t& rect, std::string text,
                       std::function<uint8_t(void)> pressHandler) :
    ButtonBase(parent, rect, pressHandler, button_create),
    text(std::move(text))
{
  initRequiredLvObj(label, text_button_label_create, [&](lv_obj_t* obj) {
    lv_label_set_text(obj, this->text.c_str());
    lv_obj_center(obj);
  });
}

void TextButton::setText(std::string value)
{
  if (value != text) {
    text = std::move(value);
    if (label) lv_label_set_text(label, text.c_str());
  }
}

#if defined(SIMU)
std::string TextButton::automationText() const
{
  auto label = Window::automationText();
  if (!label.empty()) return label;
  return text;
}

bool textButtonLabelCreateFailureFailsClosedForTest()
{
  bool pressed = false;
  bool longPressed = false;
  forceTextButtonLabelCreateFailureForTest = true;
  auto button = new (std::nothrow) TextButton(
      MainWindow::instance(), {0, 0, 100, EdgeTxStyles::UI_ELEMENT_HEIGHT},
      "Start", [&]() {
        pressed = true;
        return 0;
      });
  forceTextButtonLabelCreateFailureForTest = false;

  if (!button || !button->getLvObj() || button->isAvailable() ||
      button->isVisible() || button->automationClickable()) {
    delete button;
    return false;
  }

  button->setText("Next");
  button->setFont(FONT_STD_INDEX);
  button->setWrap();
  button->setLongPressHandler([&]() {
    longPressed = true;
    return 0;
  });
  lv_event_send(button->getLvObj(), LV_EVENT_CLICKED, nullptr);
  lv_event_send(button->getLvObj(), LV_EVENT_LONG_PRESSED, nullptr);

  const bool ok = button->automationText() == "Next" &&
                  !button->isAvailable() && !button->isVisible() && !pressed &&
                  !longPressed && !button->automationLongClickable();
  delete button;
  return ok;
}

bool touchLongPressStateIsPerWindowForTest()
{
  bool firstPressed = false;
  bool secondPressed = false;

  auto first = new (std::nothrow) TextButton(
      MainWindow::instance(), {0, 0, 100, EdgeTxStyles::UI_ELEMENT_HEIGHT},
      "First", [&]() {
        firstPressed = true;
        return 0;
      });
  auto second = new (std::nothrow) TextButton(
      MainWindow::instance(), {0, 24, 100, EdgeTxStyles::UI_ELEMENT_HEIGHT},
      "Second", [&]() {
        secondPressed = true;
        return 0;
      });

  if (!first || !second) {
    delete second;
    delete first;
    return false;
  }

  bool sent = first->sendLvEvent(LV_EVENT_LONG_PRESSED) &&
              second->sendLvEvent(LV_EVENT_CLICKED);

  bool ok = sent && !firstPressed && secondPressed;
  delete second;
  delete first;
  return ok;
}
#endif

//-----------------------------------------------------------------------------

IconButton::IconButton(Window* parent, EdgeTxIcon icon, coord_t x, coord_t y,
                       std::function<uint8_t(void)> pressHandler) :
    ButtonBase(parent, {x, y, EdgeTxStyles::UI_ELEMENT_HEIGHT, EdgeTxStyles::UI_ELEMENT_HEIGHT}, pressHandler, button_create)
{
  if (!hasLvObj()) return;

  padAll(PAD_ZERO);
  iconImage = new (std::nothrow) StaticIcon(this, 0, 0, icon, COLOR_THEME_SECONDARY1_INDEX);
  if (iconImage) {
    iconImage->center(EdgeTxStyles::UI_ELEMENT_HEIGHT - 4, EdgeTxStyles::UI_ELEMENT_HEIGHT - 4);
  }
}

void IconButton::setIcon(EdgeTxIcon icon)
{
  if (iconImage) iconImage->setIcon(icon);
}

//-----------------------------------------------------------------------------

MomentaryButton::MomentaryButton(Window* parent, const rect_t& rect, std::string text,
                       std::function<void(void)> pressHandler,
                       std::function<void(void)> releaseHandler) :
    FormField(parent, rect, button_create),
    pressHandler(std::move(pressHandler)),
    releaseHandler(std::move(releaseHandler)),
    text(std::move(text))
{
  if (!hasLvObj()) return;

  initRequiredLvObj(
      label, [](lv_obj_t* parent) { return etx_label_create(parent); },
      [&](lv_obj_t* obj) {
        lv_label_set_text(obj, this->text.c_str());
        lv_obj_center(obj);
      });
}

bool MomentaryButton::customEventHandler(lv_event_code_t code)
{
  if (!isAvailable() || !lvobj) return false;

  switch (code) {
    case LV_EVENT_PRESSED:
      if (pressHandler)
        pressHandler();
      lv_obj_add_state(lvobj, LV_STATE_CHECKED);
      lv_obj_clear_state(lvobj, LV_STATE_PRESSED);
      return true;
    case LV_EVENT_RELEASED:
      if (releaseHandler)
        releaseHandler();
      lv_obj_clear_state(lvobj, LV_STATE_CHECKED);
      return true;
    default:
      return false;
  };
}
