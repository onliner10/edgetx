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

#include "widget.h"

#include "edgetx.h"
#include "etx_lv_theme.h"
#include "menu.h"
#include "view_main.h"
#include "widget_settings.h"

#include <algorithm>
#include <new>

#if defined(HARDWARE_TOUCH)
#include "touch.h"
#endif

//-----------------------------------------------------------------------------

inline WidgetOptionValueEnum widgetValueEnumFromType(WidgetOption::Type type)
{
  switch(type) {
  case WidgetOption::String:
  case WidgetOption::File:
    return WOV_String;

  case WidgetOption::Integer:
    return WOV_Signed;

  case WidgetOption::Bool:
    return WOV_Bool;

  case WidgetOption::Source:
    return WOV_Source;

  case WidgetOption::Color:
    return WOV_Color;

  default:
    return WOV_Unsigned;
  }
}

void WidgetPersistentData::addEntry(int idx)
{
  if (idx >= (int)options.size()) {
    WidgetOptionValueTyped wov;
    wov.type = WOV_Unsigned;
    wov.value.unsignedValue = 0;
    while ((int)options.size() <= idx)
      options.push_back(wov);
    markChanged();
  }
}

bool WidgetPersistentData::hasOption(int idx)
{
  return idx < (int)options.size();
}

void WidgetPersistentData::setDefault(int idx, const WidgetOption* opt, bool forced)
{
  addEntry(idx);
  auto optType = widgetValueEnumFromType(opt->type);
  if (forced || options[idx].type != optType) {
    // reset to default value
    options[idx].type = optType;
    options[idx].value.unsignedValue = opt->deflt.unsignedValue;
    options[idx].value.stringValue = opt->deflt.stringValue;
    markChanged();
  }
}

void WidgetPersistentData::clear()
{
  if (!options.empty()) {
    options.clear();
    markChanged();
  }
}

WidgetOptionValueEnum WidgetPersistentData::getType(int idx)
{
  addEntry(idx);
  return options[idx].type;
}

void WidgetPersistentData::setType(int idx, WidgetOptionValueEnum typ)
{
  addEntry(idx);
  if (options[idx].type == typ) return;
  options[idx].type = typ;
  markChanged();
}

int32_t WidgetPersistentData::getSignedValue(int idx)
{
  addEntry(idx);
  return options[idx].value.signedValue;
}

void WidgetPersistentData::setSignedValue(int idx, int32_t newValue)
{
  addEntry(idx);
  if (options[idx].value.signedValue == newValue) return;
  options[idx].value.signedValue = newValue;
  markChanged();
}

uint32_t WidgetPersistentData::getUnsignedValue(int idx)
{
  addEntry(idx);
  return options[idx].value.unsignedValue;
}

void WidgetPersistentData::setUnsignedValue(int idx, uint32_t newValue)
{
  addEntry(idx);
  if (options[idx].value.unsignedValue == newValue) return;
  options[idx].value.unsignedValue = newValue;
  markChanged();
}

bool WidgetPersistentData::getBoolValue(int idx)
{
  addEntry(idx);
  return options[idx].value.boolValue;
}

void WidgetPersistentData::setBoolValue(int idx, bool newValue)
{
  addEntry(idx);
  if (options[idx].value.boolValue == (uint32_t)newValue) return;
  options[idx].value.boolValue = newValue;
  markChanged();
}

std::string WidgetPersistentData::getString(int idx)
{
  addEntry(idx);
  return options[idx].value.stringValue;
}

void WidgetPersistentData::setString(int idx, const char* s)
{
  addEntry(idx);
  const char* value = s ? s : "";
  if (options[idx].value.stringValue == value) return;
  options[idx].value.stringValue = value;
  markChanged();
}

//-----------------------------------------------------------------------------

WidgetPersistentData* WidgetLocation::persistentData() const
{
  switch (placement_) {
    case Placement::MainView:
      return g_model.getScreenLayoutData(screen_)->getWidgetData(zone_);
    case Placement::TopBar:
      return g_eeGeneral.getTopbarData()->getWidgetData(zone_);
  }

  return nullptr;
}

//-----------------------------------------------------------------------------

Widget::Widget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
               WidgetLocation location) :
    ButtonBase(parent, rect, nullptr, window_create),
    factory(factory),
    location(location)
{
  setWindowFlag(NO_FOCUS | NO_SCROLL);
  if (isMainViewWidget()) {
    lv_obj_add_flag(lvobj, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_add_flag(lvobj, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
  }

  setPressHandler([&]() -> uint8_t {
    // When ViewMain is in "widget select mode",
    // the widget is added to a focus group
    if (!fullscreen && lv_obj_get_group(lvobj)) openMenu();
    return 0;
  });
}

static coord_t responsive_text_padding(coord_t height)
{
  return height <= EdgeTxStyles::MENU_HEADER_HEIGHT ? PAD_TINY : PAD_SMALL;
}

FontIndex Widget::responsiveTextFont(coord_t height)
{
  static const FontIndex candidates[] = {
      FONT_XXL_INDEX, FONT_LXL_INDEX, FONT_XL_INDEX, FONT_L_INDEX,
      FONT_BOLD_INDEX, FONT_STD_INDEX, FONT_XS_INDEX, FONT_XXS_INDEX};

  coord_t pad = responsive_text_padding(height);
  coord_t contentHeight = height > 2 * pad ? height - 2 * pad : height;

  for (auto font : candidates) {
    LcdFlags flags = LcdFlags(font) << 8u;
    if (getFontHeight(flags) <= contentHeight) {
      return font;
    }
  }

  return FONT_XXS_INDEX;
}

void Widget::layoutTextLabel(lv_obj_t* label, const rect_t& rect,
                             FontIndex font, coord_t xOffset, coord_t yOffset)
{
  coord_t pad = responsive_text_padding(rect.h);
  LcdFlags flags = LcdFlags(font) << 8u;
  coord_t fontHeight = getFontHeight(flags);
  coord_t x = rect.x + pad + xOffset;
  coord_t y = rect.y + (rect.h - fontHeight) / 2 + yOffset;
  coord_t w = rect.w > 2 * pad ? rect.w - 2 * pad : rect.w;
  if (xOffset > 0 && w > xOffset) w -= xOffset;

  etx_font(label, font);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_size(label, w, fontHeight);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
}

void Widget::delayWidgetLoad()
{
  delayLoad();
}

void Widget::updateWithoutRefresh()
{
  runWidgetTask([&]() { onUpdateWithoutRefresh(); });
}

void Widget::update()
{
  runWidgetTask([&]() { onUpdate(); });
}

void Widget::background()
{
  runWidgetTask([&]() { onBackground(); });
}

void Widget::foreground()
{
  runWidgetTask([&]() { onForeground(); });
}

void Widget::onUpdateWithoutRefresh()
{
  onUpdate();
}

void Widget::openMenu()
{
  auto viewMain = ViewMain::instance();
  if (isMainViewWidget() && viewMain && viewMain->isAppMode())
  {
    setFullscreen(true);
    return;
  }

  if (hasOptions() || isMainViewWidget()) {
    Menu::open([&](Menu& menu) {
      menu.setTitle(getFactory()->getDisplayName());
      if (isMainViewWidget()) {
        menu.addLine(STR_WIDGET_FULLSCREEN, [&]() { setFullscreen(true); });
      }
      if (hasOptions()) {
        menu.addLine(STR_WIDGET_SETTINGS,
                     [=]() { new (std::nothrow) WidgetSettings(this); });
      }
    });
  }
}

#if defined(HARDWARE_KEYS)
void Widget::onLiveEvent(Window::LiveWindow& live, event_t event)
{
  if (fullscreen) {
    if (EVT_KEY_LONG(KEY_EXIT) == event) {
      setFullscreen(false);
    }
    return;
  }

  ButtonBase::onLiveEvent(live, event);
}
#endif

void Widget::onCancel()
{
  if (!fullscreen) ButtonBase::onCancel();
}

void Widget::setFullscreen(bool enable)
{
  if (!isMainViewWidget() || (enable == fullscreen)) return;

  fullscreen = enable;

  // Show or hide ViewMain widgets and decorations
  auto viewMain = ViewMain::instance();
  if (viewMain) viewMain->show(!enable);
  Messaging::send(Messaging::DECORATION_UPDATE);

  // Leave Fullscreen Mode
  if (!enable) {
    clearWindowFlag(OPAQUE);

    lv_group_remove_obj(lvobj);

    // re-enable scroll chaining (sliding main view)
    lv_obj_add_flag(lvobj, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_add_flag(lvobj, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
  }
  // Enter Fullscreen Mode
  else {
    if (viewMain) viewMain->enableWidgetSelect(false);

    // ViewMain hidden - re-show this widget
    show();

    // Set window opaque (inhibits redraw from windows below)
    setWindowFlag(OPAQUE);

    updateZoneRect(parent->getRect(), false);
    setRect(parent->getRect());

    if (!lv_obj_get_group(lvobj))
      lv_group_add_obj(lv_group_get_default(), lvobj);

    // disable scroll chaining (sliding main view)
    lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
  }

  // set group in editing mode (keys LEFT / RIGHT)
  if (enableFullScreenRE())
    lv_group_set_editing(lv_group_get_default(), enable);

  onFullscreen(enable);

  if (fullscreen)
    updateWithoutRefresh();
}

bool Widget::onLiveLongPress(Window::LiveWindow&)
{
  if (!fullscreen) {
    openMenu();
    return false;
  }
  return true;
}

const WidgetOption* Widget::getOptionDefinitions() const
{
  return getFactory()->getDefaultOptions();
}

void Widget::enableFocus(bool enable)
{
  if (enable) {
    if (!focusBorder) {
      lv_style_init(&borderStyle);
      lv_style_set_line_width(&borderStyle, PAD_BORDER);
      lv_style_set_line_opa(&borderStyle, LV_OPA_COVER);
      lv_style_set_line_color(&borderStyle, makeLvColor(COLOR_THEME_ACTIVE));

      borderPts[0] = {1, 1};
      borderPts[1] = {(lv_coord_t)(width() - 1), 1};
      borderPts[2] = {(lv_coord_t)(width() - 1), (lv_coord_t)(height() - 1)};
      borderPts[3] = {1, (lv_coord_t)(height() - 1)};
      borderPts[4] = {1, 1};

      focusBorder = lv_line_create(lvobj);
      lv_obj_add_style(focusBorder, &borderStyle, LV_PART_MAIN);
      lv_line_set_points(focusBorder, borderPts, 5);

      if (!hasFocus()) {
        lv_obj_add_flag(focusBorder, LV_OBJ_FLAG_HIDDEN);
      }

      setFocusHandler([=](bool hasFocus) {
        if (hasFocus) {
          lv_obj_clear_flag(focusBorder, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(focusBorder, LV_OBJ_FLAG_HIDDEN);
        }
        auto viewMain = ViewMain::instance();
        if (viewMain) viewMain->refreshWidgetSelectTimer();
      });

      lv_group_add_obj(lv_group_get_default(), lvobj);
    }
  } else {
    if (focusBorder) {
      lv_obj_del(focusBorder);
      setFocusHandler(nullptr);
      lv_group_remove_obj(lvobj);
    }
    focusBorder = nullptr;
  }
}

WidgetPersistentData* Widget::getPersistentData() { return location.persistentData(); }

uint32_t Widget::getPersistentDataRevision()
{
  auto widgetData = getPersistentData();
  return widgetData ? widgetData->getRevision() : 0;
}

TrackedWidget::TrackedWidget(const WidgetFactory* factory, Window* parent,
                             const rect_t& rect, WidgetLocation location,
                             LoadMode loadMode) :
    Widget(factory, parent, rect, location)
{
  switch (loadMode) {
    case LoadMode::Immediate:
      markLoaded();
      break;
    case LoadMode::Delayed:
      delayLoad();
      break;
  }
}

void TrackedWidget::onForeground()
{
  WidgetRefreshKey key;
  key.add(getPersistentDataRevision())
     .add((int32_t)width())
     .add((int32_t)height())
     .add(isFullscreen())
     .add(refreshKey());

  uint32_t currentKey = key.value();
  if (!refreshPending && currentKey == lastRefreshKey) return;

  lastRefreshKey = currentKey;
  refreshPending = false;
  refresh();
}

//-----------------------------------------------------------------------------

WidgetFactory::~WidgetFactory()
{
  unregisterWidget(*this);
}

WidgetFactory::RegisteredWidgets& WidgetFactory::registeredWidgets()
{
  static RegisteredWidgets widgets;
  return widgets;
}

const WidgetFactory::RegisteredWidgets& WidgetFactory::getRegisteredWidgets()
{
  return registeredWidgets();
}

void WidgetFactory::unregisterWidget(const WidgetFactory& factory)
{
  auto& widgets = registeredWidgets();
  widgets.erase(
      std::remove_if(widgets.begin(), widgets.end(),
                     [&](const auto& registered) {
                       return &registered.get() == &factory;
                     }),
      widgets.end());
}

const WidgetFactory* WidgetFactory::getWidgetFactory(const char* name)
{
  if (!name) return nullptr;

  for (const auto& registered : getRegisteredWidgets()) {
    const auto& factory = registered.get();
    if (!strcmp(name, factory.getName())) {
      return &factory;
    }
  }
  return nullptr;
}

void WidgetFactory::registerWidget(const WidgetFactory& factory)
{
  auto& widgets = registeredWidgets();
  auto name = factory.getName();
  auto oldWidget = getWidgetFactory(name);
  if (oldWidget) {
    unregisterWidget(*oldWidget);
  }
  for (auto it = widgets.cbegin(); it != widgets.cend(); ++it) {
    if (strcasecmp(it->get().getDisplayName(), factory.getDisplayName()) > 0) {
      widgets.insert(it, std::cref(factory));
      return;
    }
  }
  widgets.push_back(std::cref(factory));
}

Widget* WidgetFactory::newWidget(const char* name, Window* parent,
                                 const rect_t& rect, WidgetLocation location)
{
  const WidgetFactory* factory = getWidgetFactory(name);
  if (factory) {
    return factory->create(parent, rect, location, false);
  }
  return nullptr;
}

Widget* WidgetFactory::create(Window* parent, const rect_t& rect,
                              WidgetLocation location, bool init) const
{
  auto widgetData = location.persistentData();
  if (!widgetData) return nullptr;

  if (init) {
    widgetData->clear();
    parseOptionDefaults();
  }
  if (options) {
    checkOptions(location);
    int i = 0;
    for (const WidgetOption* option = options; option->name; option++, i++) {
      TRACE("WidgetFactory::create() setting option '%s'", option->name);
      widgetData->setDefault(i, option, init);
    }
  }

  return createNew(parent, rect, location);
}
