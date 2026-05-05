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

#include "topbar.h"

#include "edgetx.h"
#include "storage/storage.h"
#include "etx_lv_theme.h"
#include "topbar.h"
#include "view_main.h"
#include "widgets_setup.h"
#include "pagegroup.h"
#include "theme_manager.h"
#include "widget.h"

#include <new>

//-----------------------------------------------------------------------------

void TopBarPersistentData::clearZone(int idx)
{
  zones[idx].clear();
}

void TopBarPersistentData::clear()
{
  for (int i = 0; i < MAX_TOPBAR_ZONES; i += 1)
    clearZone(i);
}

const char* TopBarPersistentData::getWidgetName(int idx)
{
  return zones[idx].widgetName.c_str();
}

void TopBarPersistentData::setWidgetName(int idx, const char* s)
{
  zones[idx].widgetName = s;
}

WidgetPersistentData* TopBarPersistentData::getWidgetData(int idx)
{
  return &zones[idx].widgetData;
}

bool TopBarPersistentData::hasWidget(int idx)
{
  return !zones[idx].widgetName.empty();
}

bool TopBarPersistentData::isWidget(int idx, const char* s)
{
  return zones[idx].widgetName == s;
}

//-----------------------------------------------------------------------------

SetupTopBarWidgetsPage::SetupTopBarWidgetsPage() :
    NavWindow(ViewMain::instance(), rect_t{})
{
  // remember focus
  pushLayer();

  auto viewMain = ViewMain::instance();

  // save current view & switch to 1st one
  viewMain->setCurrentMainView(0);

  // adopt the dimensions of the main view
  setRect(viewMain->getRect());

  auto topbar = viewMain->getTopbar();
  topbar->setSetupMode(true);
  topbar->updateZones();
  for (unsigned i = 0; i < topbar->getZonesCount(); i++) {
    auto rect = topbar->getZone(i);
    new (std::nothrow) SetupWidgetsPageSlot(this, rect, topbar, i);
  }

#if defined(HARDWARE_TOUCH)
  addBackButton();
#endif
}

void SetupTopBarWidgetsPage::onClicked()
{
  // block event forwarding (window is transparent)
}

void SetupTopBarWidgetsPage::onCancel() { deleteLater(); }

void SetupTopBarWidgetsPage::deleteLater()
{
  if (_deleted) return;

  // and continue async deletion...
  Window::deleteLater();

  // restore screen setting tab on top
  QuickMenu::openPage(QM_UI_SETUP);

  auto viewMain = ViewMain::instance();
  if (viewMain && viewMain->getTopbar()) {
    viewMain->getTopbar()->setSetupMode(false);
    viewMain->getTopbar()->load();
  }

  storageDirty(EE_GENERAL);
}

//-----------------------------------------------------------------------------

TopBar::TopBar(Window * parent) :
  WidgetsContainer(parent, {0, 0, LCD_W, EdgeTxStyles::MENU_HEADER_HEIGHT}, MAX_TOPBAR_ZONES)
{
  setWindowFlag(NO_FOCUS);
  etx_solid_bg(lvobj, COLOR_THEME_SECONDARY1_INDEX);

  headerIcon = new (std::nothrow) HeaderIcon(parent, ICON_EDGETX, [=]() { QuickMenu::openQuickMenu(); });
}

unsigned int TopBar::getZonesCount() const
{
  int last = -1;
  for (int i = 0; i < zoneCount; i += 1) {
    if (hasLayoutWidget(i)) last = i;
  }

  // Keep one empty setup slot after the last configured widget so the topbar
  // behaves like an ordered list instead of a fixed-width grid.
  unsigned int count = last < 0 ? 1 : last + 2;
  if (count > zoneCount) count = zoneCount;
  return count;
}

rect_t TopBar::getZone(unsigned int index) const
{
  if (index >= zoneCount) return rect_t{};

  const coord_t left = MENU_HEADER_BUTTONS_LEFT + 1;
  const coord_t right = LCD_W - PAD_TINY;
  const coord_t gap = PAD_MEDIUM;
  const int first = firstLayoutWidget();
  const bool layoutWidget = hasLayoutWidget(index);
  const int pendingSetupIndex =
      setupMode && !hasLayoutWidget(getZonesCount() - 1) ? getZonesCount() - 1
                                                         : -1;
  const bool pendingSetupSlot = !layoutWidget && (int)index == pendingSetupIndex;

  if (first < 0 || index == (unsigned int)first) {
    coord_t x = left;
    coord_t w = right - left;

    coord_t statusWidth = 0;
    unsigned int statusCount = 0;
    for (unsigned int i = (first < 0 ? 1 : first + 1); i < zoneCount; i += 1) {
      if (!hasLayoutWidget(i) && (int)i != pendingSetupIndex) continue;
      statusWidth += intrinsicZoneWidth(i);
      statusCount += 1;
    }

    if (statusCount > 0) statusWidth += statusCount * gap;
    if (statusWidth > 0 && w > statusWidth) w -= statusWidth;
    if (w < TOPBAR_FLEX_MIN_WIDTH) w = TOPBAR_FLEX_MIN_WIDTH;
    if (x + w > right) w = right - x;

    return {x, PAD_THREE, w, TOPBAR_ZONE_HEIGHT};
  }

  if (!layoutWidget && !pendingSetupSlot) {
    return {left, PAD_THREE, TOPBAR_FLEX_MIN_WIDTH, TOPBAR_ZONE_HEIGHT};
  }

  coord_t x = right;
  for (int i = zoneCount - 1; i > first; i -= 1) {
    bool useSlot = hasLayoutWidget(i) || i == pendingSetupIndex;
    if (!useSlot) continue;

    coord_t w = intrinsicZoneWidth(i);
    x -= w;
    if (i == (int)index) return {x, PAD_THREE, w, TOPBAR_ZONE_HEIGHT};
    x -= gap;
  }

  return {left, PAD_THREE, TOPBAR_FLEX_MIN_WIDTH, TOPBAR_ZONE_HEIGHT};
}

bool TopBar::hasLayoutWidget(unsigned int index) const
{
  if (index >= zoneCount) return false;
  return g_eeGeneral.getTopbarData()->hasWidget(index);
}

int TopBar::firstLayoutWidget() const
{
  for (int i = 0; i < zoneCount; i += 1) {
    if (hasLayoutWidget(i)) return i;
  }
  return -1;
}

coord_t TopBar::intrinsicZoneWidth(unsigned int index) const
{
  if (index >= zoneCount) return TOPBAR_STATUS_WIDTH;

  const char* name = g_eeGeneral.getTopbarData()->getWidgetName(index);
  if (!strcmp(name, "Date Time")) return TOPBAR_DATETIME_WIDTH;
  if (!strcmp(name, "Link")) return TOPBAR_LINK_WIDTH;
  if (!strcmp(name, "TX Battery")) return TOPBAR_BATTERY_WIDTH;
  if (!strcmp(name, "Volume")) return TOPBAR_VOLUME_WIDTH;
  if (!strcmp(name, "Internal GPS")) return TOPBAR_GPS_WIDTH;
  if (!strcmp(name, "Radio Info")) return TOPBAR_LEGACY_STATUS_WIDTH;

  return TOPBAR_STATUS_WIDTH;
}

void TopBar::compactLayoutWidgets()
{
  auto topbarData = g_eeGeneral.getTopbarData();
  unsigned int writeIndex = 0;
  bool changed = false;

  for (unsigned int readIndex = 0; readIndex < zoneCount; readIndex += 1) {
    if (!topbarData->hasWidget(readIndex)) continue;

    if (readIndex != writeIndex) {
      topbarData->zones[writeIndex] = topbarData->zones[readIndex];
      topbarData->clearZone(readIndex);
      changed = true;
    }
    writeIndex += 1;
  }

  if (changed) storageDirty(EE_GENERAL);
}

void TopBar::setVisible(float visible) // 0.0 -> 1.0
{
  coord_t y = 0;
  if (visible == 0.0) {
    y = -EdgeTxStyles::MENU_HEADER_HEIGHT;
  } else if (visible > 0.0 && visible < 1.0){
    y = -(float)EdgeTxStyles::MENU_HEADER_HEIGHT * (1.0 - visible);
  }
  if (y != top()) setTop(y);
}

void TopBar::setEdgeTxButtonVisible(float visible) // 0.0 -> 1.0
{
  coord_t y = 0;
  if (visible == 0.0) {
    y = -EdgeTxStyles::MENU_HEADER_HEIGHT;
  } else if (visible > 0.0 && visible < 1.0){
    y = -(float)EdgeTxStyles::MENU_HEADER_HEIGHT * (1.0 - visible);
  }
  if (headerIcon && y != headerIcon->top()) headerIcon->setTop(y);
}

coord_t TopBar::getVisibleHeight(float visible) const // 0.0 -> 1.0
{
  if (visible == 0.0) {
    return 0;
  }
  else if (visible == 1.0) {
    return EdgeTxStyles::MENU_HEADER_HEIGHT;
  }

  float h = (float)EdgeTxStyles::MENU_HEADER_HEIGHT * visible;
  return (coord_t)h;
}

void TopBar::removeWidget(unsigned int index)
{
  if (index >= zoneCount) return;

  bool mark = false;

  // If user manually removes 'system' widgets, mark name so widget does not get reloaded on restart
  if ((index == (unsigned int)(zoneCount - 1)) && g_eeGeneral.getTopbarData()->isWidget(index, "Date Time"))
    mark = true;
  if ((index == (unsigned int)(zoneCount - 2)) &&
      (g_eeGeneral.getTopbarData()->isWidget(index, "Volume") ||
       g_eeGeneral.getTopbarData()->isWidget(index, "Radio Info")))
    mark = true;
  if ((zoneCount > 2) && (index == (unsigned int)(zoneCount - 3)) &&
      g_eeGeneral.getTopbarData()->isWidget(index, "TX Battery"))
    mark = true;
  if ((zoneCount > 3) && (index == (unsigned int)(zoneCount - 4)) &&
      g_eeGeneral.getTopbarData()->isWidget(index, "Link"))
    mark = true;
#if defined(INTERNAL_GPS)
  if ((zoneCount > 4) && (index == (unsigned int)(zoneCount - 5)) &&
      g_eeGeneral.getTopbarData()->isWidget(index, "Internal GPS"))
    mark = true;
#endif

  // If user manually removes 'system' widgets, mark name so widget does not get reloaded on restart
  if (mark)
    g_eeGeneral.getTopbarData()->setWidgetName(index, "---");

  g_eeGeneral.getTopbarData()->clearZone(index);

  WidgetsContainer::removeWidget(index);
}

void TopBar::load()
{
  if (!widgets) return;
  compactLayoutWidgets();

  for (unsigned int i = 0; i < zoneCount; i++) {
    // remove old widget
    if (widgets[i]) {
      widgets[i]->deleteLater();
      widgets[i] = nullptr;
    }
  }

  for (unsigned int i = 0; i < zoneCount; i++) {
    // and load new one if required
    if (g_eeGeneral.getTopbarData()->hasWidget(i)) {
      widgets[i] = WidgetFactory::newWidget(g_eeGeneral.getTopbarData()->getWidgetName(i), this, getZone(i), -1, i);
    }
  }
}

Widget* TopBar::createWidget(unsigned int index,
                      const WidgetFactory* factory)
{
  if (!widgets || index >= zoneCount) return nullptr;

  // remove old one if existing
  removeWidget(index);

  Widget* widget = nullptr;
  if (factory) {
    g_eeGeneral.getTopbarData()->setWidgetName(index, factory->getName());
    widget = factory->create(this, getZone(index), -1, index);
  }
  widgets[index] = widget;

  return widget;
}

void TopBar::create()
{
  g_eeGeneral.getTopbarData()->clear();
}

//-----------------------------------------------------------------------------
