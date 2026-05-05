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

#include "button.h"
#include "widgets_container.h"

class ScreenMenu;
class SetupTopBarWidgetsPage;

class SetupWidgetsPage : public NavWindow
{
 public:
  SetupWidgetsPage(uint8_t customScreenIdx);

#if defined(DEBUG_WINDOWS)
  std::string getName() const override
  {
    return "SetupWidgetsPage(idx=" + std::to_string(customScreenIdx) + ")";
  }
#endif

  void onClicked() override;
  void onCancel() override;

#if defined(HARDWARE_KEYS)
  void onPressSYS() override {}
  void onLongPressSYS() override {}
  void onPressMDL() override {}
  void onLongPressMDL() override {}
  void onPressPGUP() override {}
  void onPressPGDN() override {}
  void onLongPressPGUP() override {}
  void onLongPressPGDN() override {}
#endif

 protected:
  uint8_t customScreenIdx;
  unsigned savedView = 0;

  void deleteLater() override;
};

class SetupWidgetsPageSlot : public ButtonBase
{
 public:
  SetupWidgetsPageSlot(Window* parent, const rect_t& rect,
                       WidgetsContainer* container, uint8_t slotIndex,
                       SetupTopBarWidgetsPage* topBarSetupPage = nullptr);
  void setSlotRect(const rect_t& rect);

 protected:
  WidgetsContainer* container = nullptr;
  SetupTopBarWidgetsPage* topBarSetupPage = nullptr;
  WidgetSlotIndex slot;
  bool openSettings = false;
  lv_style_t borderStyle;
  lv_point_t borderPts[5];
  lv_obj_t* border = nullptr;

  void setFocusState();
  void updateBorder();

  void addNewWidget();
  void moveWidget(WidgetMoveDirection direction);
};
