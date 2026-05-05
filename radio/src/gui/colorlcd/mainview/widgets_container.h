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

#pragma once

#include "window.h"

class Widget;
class WidgetFactory;

enum class WidgetMoveDirection : int8_t {
  Left = -1,
  Right = 1,
};

class WidgetSlotIndex
{
 public:
  explicit constexpr WidgetSlotIndex(uint8_t value) : value_(value) {}

  constexpr uint8_t value() const { return value_; }
  constexpr unsigned int asUnsigned() const { return value_; }

 private:
  uint8_t value_;
};

constexpr bool operator==(WidgetSlotIndex lhs, WidgetSlotIndex rhs)
{
  return lhs.value() == rhs.value();
}

constexpr bool operator!=(WidgetSlotIndex lhs, WidgetSlotIndex rhs)
{
  return !(lhs == rhs);
}

struct WidgetMoveResult
{
  WidgetSlotIndex from;
  WidgetSlotIndex to;

  constexpr bool moved() const { return from != to; }
};

class WidgetsContainer: public Window
{
 public:
  WidgetsContainer(Window* parent, const rect_t& rect, uint8_t zoneCount);

  virtual unsigned int getZonesCount() const = 0;
  virtual rect_t getZone(unsigned int index) const = 0;
  virtual Widget * createWidget(unsigned int index, const WidgetFactory * factory) = 0;

  Widget* getWidget(unsigned int index);
  virtual void removeWidget(unsigned int index);
  virtual bool canMoveWidget(WidgetSlotIndex,
                             WidgetMoveDirection) const { return false; }
  [[nodiscard]] virtual WidgetMoveResult moveWidget(WidgetSlotIndex index,
                                                    WidgetMoveDirection)
  {
    return {index, index};
  }
  void removeAllWidgets();
  void updateZones();
  void showWidgets(bool visible = true);
  void hideWidgets() { showWidgets(false); }
  void refreshWidgets(bool inForeground);

  virtual bool isLayout() { return false; }
  virtual bool isAppMode() const { return false; }

  void deleteLater() override;

 protected:
  uint8_t zoneCount = 0;
  Widget** widgets = nullptr;
};
