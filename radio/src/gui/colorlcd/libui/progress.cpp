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

#include "progress.h"

#include "etx_lv_theme.h"
#include "mainwindow.h"

#include <new>

#if defined(SIMU)
static bool forceProgressBarCreateFailure = false;

static void progressForceBarCreateFailureForTest(bool force)
{
  forceProgressBarCreateFailure = force;
}
#endif

static void etx_bar_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
  etx_solid_bg(obj, COLOR_THEME_SECONDARY2_INDEX);
  etx_obj_add_style(obj, styles->rounded, LV_PART_MAIN);

  etx_solid_bg(obj, COLOR_THEME_SECONDARY1_INDEX, LV_PART_INDICATOR);
  etx_obj_add_style(obj, styles->rounded, LV_PART_INDICATOR);
}

static const lv_obj_class_t bar_class = {
    .base_class = &lv_bar_class,
    .constructor_cb = etx_bar_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = LV_PCT(100),
    .height_def = EdgeTxStyles::UI_ELEMENT_HEIGHT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(lv_bar_t),
};

static lv_obj_t* bar_create(lv_obj_t* parent)
{
#if defined(SIMU)
  if (forceProgressBarCreateFailure) return nullptr;
#endif
  return etx_create(&bar_class, parent);
}

Progress::Progress(Window* parent, const rect_t& rect) : Window(parent, rect)
{
  if (!initRequiredLvObj(bar, bar_create, [&](lv_obj_t* obj) {
        lv_bar_set_range(obj, 0, 100);
      }))
    return;
  setValue(0);
}

void Progress::setValue(int newValue)
{
  if (newValue != value) {
    value = newValue;
    if (isAvailable() && bar) lv_bar_set_value(bar, value, LV_ANIM_OFF);
  }
}

#if defined(SIMU)
bool progressBarAllocationFailureFailsClosedForTest()
{
  progressForceBarCreateFailureForTest(true);
  auto progress =
      new (std::nothrow) Progress(MainWindow::instance(), {0, 0, 120, 20});
  progressForceBarCreateFailureForTest(false);

  if (!progress) return false;
  progress->setValue(42);

  bool ok = !progress->isAvailable() && !progress->isVisible();
  delete progress;
  return ok;
}
#endif
