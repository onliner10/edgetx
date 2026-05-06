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

#include "listbox.h"

#include "debug.h"
#include "mainwindow.h"

#include <new>

ListBox::ListBox(Window* parent, const rect_t& rect,
                 const std::vector<std::string>& names, uint8_t lineHeight) :
    TableField(parent, rect)
{
  setColumnWidth(0, rect.w);

  setLineHeight(lineHeight);
  setNames(names);
}

void ListBox::setName(uint16_t idx, const std::string& name)
{
  withLive([&](LiveWindow& live) {
    auto obj = live.lvobj();
    lv_table_set_cell_value(obj, idx, 0, name.c_str());
  });
}

void ListBox::setNames(const std::vector<std::string>& names)
{
  setRowCount(names.size());

  uint16_t row = 0;
  for (const auto& name : names) {
    setName(row, name);
    row++;
  }
}

void ListBox::setLineHeight(uint8_t height)
{
  withLive([&](LiveWindow& live) {
    auto obj = live.lvobj();
    lv_obj_set_style_max_height(obj, height, LV_PART_ITEMS);
  });
}

void ListBox::setSelected(int selected, bool force)
{
  select(selected, 0, force);
}

void ListBox::setSelected(std::set<uint32_t> selected)
{
  if (!multiSelect) return;

  withLive([&](LiveWindow& live) {
    auto obj = live.lvobj();
    for (int i = 0; i < getRowCount(); i++) {
      if (selected.find(i) != selected.end())
        lv_table_add_cell_ctrl(obj, i, 0, LV_TABLE_CELL_CTRL_CUSTOM_1);
      else
        lv_table_clear_cell_ctrl(obj, i, 0, LV_TABLE_CELL_CTRL_CUSTOM_1);
    }
  });
}

bool ListBox::isRowSelected(uint16_t row)
{
  bool selected = false;
  withLive([&](LiveWindow& live) {
    auto obj = live.lvobj();
    selected = lv_table_has_cell_ctrl(obj, row, 0, LV_TABLE_CELL_CTRL_CUSTOM_1);
  });
  return selected;
}

std::set<uint32_t> ListBox::getSelection()
{
  std::set<uint32_t> selectedIndexes;
  if (multiSelect) {
    withLive([&](LiveWindow& live) {
      auto obj = live.lvobj();
      for (int i = 0; i < getRowCount(); i++) {
        if (lv_table_has_cell_ctrl(obj, i, 0, LV_TABLE_CELL_CTRL_CUSTOM_1))
          selectedIndexes.insert(i);
      }
    });
  }
  return selectedIndexes;
}

void ListBox::setActiveItem(int item)
{
  if (item != activeItem) {
    activeItem = item;
  }
}

int ListBox::getActiveItem() const { return activeItem; }

void ListBox::onPress(uint16_t row, uint16_t col)
{
  withLive([&](LiveWindow& live) {
    auto obj = live.lvobj();
    if (row == LV_TABLE_CELL_NONE) return;

    TRACE("SHORT_PRESS");

    if (multiSelect && row < getRowCount()) {
      std::set<uint32_t> lastSelection = getSelection();

      bool chk = lv_table_has_cell_ctrl(obj, row, 0, LV_TABLE_CELL_CTRL_CUSTOM_1);
      if (chk)
        lv_table_clear_cell_ctrl(obj, row, 0, LV_TABLE_CELL_CTRL_CUSTOM_1);
      else
        lv_table_add_cell_ctrl(obj, row, 0, LV_TABLE_CELL_CTRL_CUSTOM_1);

      if (_multiSelectHandler) {
        _multiSelectHandler(getSelection(), lastSelection);
      }
    } else {
      if (pressHandler) {
        pressHandler();
      }
    }
  });
}

void ListBox::onLiveClicked(Window::LiveWindow& live)
{
  lv_group_set_editing((lv_group_t*)lv_obj_get_group(live.lvobj()), true);
}

void ListBox::onCancel()
{
  bool handled = false;
  withLive([&](LiveWindow& live) {
    auto group = (lv_group_t*)lv_obj_get_group(live.lvobj());
    if (!isAutoEdit() && group && lv_group_get_editing(group)) {
      lv_group_set_editing(group, false);
      handled = true;
    }
  });
  if (!handled) TableField::onCancel();
}

void ListBox::onDrawEnd(uint16_t row, uint16_t col, lv_obj_draw_part_dsc_t* dsc)
{
  withLive([&](LiveWindow& live) {
    auto obj = live.lvobj();

    if ((multiSelect == false && row != activeItem) ||
        (multiSelect == true &&
         !lv_table_has_cell_ctrl(obj, dsc->id, 0, LV_TABLE_CELL_CTRL_CUSTOM_1)))
      return;

    lv_area_t coords;

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.font = dsc->label_dsc->font;
    label_dsc.align = LV_TEXT_ALIGN_RIGHT;

    const char* sym = LV_SYMBOL_OK;
    if (getSelectedSymbol) sym = getSelectedSymbol(row);

    lv_coord_t w = 30;
    lv_coord_t h = 12;
    lv_coord_t xo = 1;
    lv_coord_t yo = 1;

    if (smallSelectMarker) {
      // Check for non-LVGL symbol
      if (sym[0] != (char)0xEF) {
        yo = -2;
        xo = 0;
      }
      label_dsc.font = getFont(FONT(XS));
    } else {
      h = getFontHeight(FONT(STD));
      xo = 2;
      yo = (lv_area_get_height(dsc->draw_area) - h) / 2;
    }

    coords.x2 = dsc->draw_area->x2 - xo - PAD_MEDIUM;
    coords.x1 = coords.x2 - w + 1;
    coords.y1 = dsc->draw_area->y1 + yo;
    coords.y2 = coords.y1 + h - 1;

    lv_draw_label(dsc->draw_ctx, &label_dsc, &coords, sym, nullptr);
  });
}

#if defined(SIMU)
void etxCreateForceObjectAllocationFailureForTest(bool force);

bool listBoxObjectAllocationFailureFailsClosedForTest()
{
  class TestListBox : public ListBox
  {
   public:
    explicit TestListBox(Window* parent) :
        ListBox(parent, {0, 0, 120, 80}, {"A", "B"})
    {
    }

    void exerciseProtectedHandlers()
    {
      onPress(0, 0);
      onClicked();
      onCancel();
    }
  };

  etxCreateForceObjectAllocationFailureForTest(true);
  auto list = new (std::nothrow) TestListBox(MainWindow::instance());
  etxCreateForceObjectAllocationFailureForTest(false);

  if (!list) return false;

  list->setName(0, "C");
  list->setNames({"C", "D"});
  list->setLineHeight(ListBox::MENUS_LINE_HEIGHT);
  list->setSelected(0);
  list->setMultiSelect(true);
  list->setSelected({0});
  (void)list->isRowSelected(0);
  (void)list->getSelection();
  list->setActiveItem(0);
  list->exerciseProtectedHandlers();

  bool ok = !list->isAvailable() && !list->isVisible() &&
            !list->automationClickable();
  delete list;
  return ok;
}
#endif
