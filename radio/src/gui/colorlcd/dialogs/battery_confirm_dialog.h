/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once

#include "libui/fullscreen_dialog.h"

class BatteryConfirmDialog : public FullScreenDialog
{
 public:
  BatteryConfirmDialog(uint8_t monitor, uint16_t packMask);

 protected:
  uint8_t monitor;
  uint16_t packMask;

  void buildBody();
  void onConfirmPack(uint8_t slot);
  void closeDialog();
};