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

#pragma once

#include "window.h"

class FormField;

class Keyboard : public NavWindow
{
 public:
  explicit Keyboard(coord_t height, bool fullScreen = false);
  ~Keyboard();

  void clearField(bool wasCancelled, bool notifyField = true);
  static void hide(bool wasCancelled);
  static void detachField(FormField* field);

  static Keyboard* keyboardWindow() { return activeKeyboard; }

 protected:
  static Keyboard* activeKeyboard;

  bool hasTwoPageKeys;
  lv_group_t* group = nullptr;
  RequiredLvObj keyboardObj;
  lv_obj_t* keyboard = nullptr;

  FormField* field = nullptr;
  Window* fieldContainer = nullptr;
  lv_group_t* fieldGroup = nullptr;
  lv_coord_t scroll_pos = 0;
  bool fullScreen = false;

  struct FieldBinding {
    FormField* field;
    lv_obj_t* obj;
    Window* container;
  };

  template <typename T>
  void runPageKey(T& keyboard, void (T::*singlePageAction)(),
                  void (T::*twoPageAction)()) const
  {
    (keyboard.*(hasTwoPageKeys ? twoPageAction : singlePageAction))();
  }

  void showKeyboard();
  bool isKeyboardReady() const
  {
    return withKeyboardParts([](LiveWindow&, lv_obj_t*, lv_group_t*) {});
  }
  bool bindField(FormField* newField, FieldBinding& binding) const;

  template <typename Fn>
  bool withKeyboardParts(Fn&& fn) const
  {
    return withLive([&](LiveWindow& live) {
      if (!group) return false;
      return keyboardObj.with([&](lv_obj_t* keyboard) {
        using Result = std::invoke_result_t<Fn, LiveWindow&, lv_obj_t*,
                                            lv_group_t*>;
        if constexpr (std::is_void_v<Result>) {
          fn(live, keyboard, group);
          return true;
        } else {
          return static_cast<bool>(fn(live, keyboard, group));
        }
      });
    });
  }

  template <typename T>
  static void discardKeyboard(T*& instance)
  {
    auto keyboard = instance;
    instance = nullptr;
    if (!keyboard) return;
    if (activeKeyboard == keyboard) {
      activeKeyboard = nullptr;
      keyboard->clearField(true);
    }
    keyboard->deleteLater();
  }

  template <typename T>
  static T* liveKeyboard(T*& instance)
  {
    if (instance && !instance->isKeyboardReady()) discardKeyboard(instance);
    if (!instance) instance = Window::makeLive<T>();
    if (instance && !instance->isKeyboardReady()) discardKeyboard(instance);
    return instance;
  }

  template <typename T, typename Configure>
  static T* openKeyboard(T*& instance, FormField* newField,
                         Configure&& configure)
  {
    auto keyboard = liveKeyboard(instance);

    FieldBinding binding{};
    if (!keyboard || !keyboard->bindField(newField, binding)) return nullptr;

    if (!keyboard->setField(binding)) {
      if (!keyboard->isKeyboardReady()) discardKeyboard(instance);
      return nullptr;
    }
    configure(*keyboard);
    keyboard->showKeyboard();
    return keyboard;
  }

  bool setField(const FieldBinding& binding);
  bool attachKeyboard();
  void onDelete() override;
};
