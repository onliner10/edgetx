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

#include "screen_user_interface.h"

#include "choice.h"
#include "file_preview.h"
#include "theme_manager.h"
#include "view_main.h"

#include <new>

#if LANDSCAPE

// form grid
static const lv_coord_t line_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1),
                                          LV_GRID_TEMPLATE_LAST};
static const lv_coord_t line_row_dsc[] = {LV_GRID_CONTENT,
                                          LV_GRID_TEMPLATE_LAST};

#else

// form grid
static const lv_coord_t line_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(2),
                                          LV_GRID_TEMPLATE_LAST};
static const lv_coord_t line_row_dsc[] = {LV_GRID_CONTENT,
                                          LV_GRID_TEMPLATE_LAST};

#endif

ScreenUserInterfacePage::ScreenUserInterfacePage(const PageDef& pageDef) :
    PageGroupItem(pageDef, PAD_TINY)
{
}

void ScreenUserInterfacePage::build(Window* window)
{
  window->padAll(PAD_TINY);
  window->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_ZERO);

  FlexGridLayout grid(line_col_dsc, line_row_dsc, PAD_TINY);

  // Top Bar
  auto line = window->newLine(grid);
  new (std::nothrow) StaticText(line, rect_t{}, STR_TOP_BAR);

  new (std::nothrow) TextButton(line, rect_t{}, STR_SETUP_WIDGETS,
            [=]() -> uint8_t {
                window->getParent()->deleteLater();
                new (std::nothrow) SetupTopBarWidgetsPage();
                return 0;
            });
}
