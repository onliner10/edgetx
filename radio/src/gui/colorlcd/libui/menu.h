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

#include <new>

#include "modal_window.h"

class MaskBitmap;
class Menu;
class MenuContent;
class MenuToolbar;

class Menu : public ModalWindow
{
 public:
  explicit Menu(bool multiple = false, coord_t popupWidth = 0);

  template <typename Fn>
  static bool open(Fn&& build, bool multiple = false, coord_t popupWidth = 0)
  {
    auto menu = new (std::nothrow) Menu(multiple, popupWidth);
    if (!menu) return false;
    build(*menu);
    return true;
  }

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "Menu"; }
#endif

  void setCancelHandler(std::function<void()> handler);
  void setWaitHandler(std::function<void()> handler);
  void setLongPressHandler(std::function<void()> handler);

  void setToolbar(MenuToolbar* window);

  void setTitle(std::string text);

  void addLine(const MaskBitmap* icon_mask, const std::string& text,
               std::function<void()> onPress,
               std::function<bool()> isChecked = nullptr);

  void addLine(const std::string& text, std::function<void()> onPress,
               std::function<bool()> isChecked = nullptr);

  void addLineBuffered(const std::string& text, std::function<void()> onPress,
                       std::function<bool()> isChecked = nullptr);

  void updateLines();

  void removeLines();

  unsigned count() const;

  int selection() const;

  void select(int index);

  void onCancel() override;
  void onLiveCheckEvents(LiveWindow& live) override;

  void handleLongPress();

  bool isMultiple() const { return multiple; }

 protected:
  bool multiple;
  MenuContent& content;
  MenuToolbar* toolbar = nullptr;
  std::function<void()> waitHandler;
  std::function<void()> cancelHandler;
  std::function<void()> longPressHandler;

  MenuContent& createContent(coord_t popupWidth);
  void updatePosition();
};
