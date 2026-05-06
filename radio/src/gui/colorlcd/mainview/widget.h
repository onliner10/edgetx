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

#include <stdint.h>
#include <string.h>
#include <functional>
#include <new>
#include <vector>

#include "button.h"
#include "widgets_container.h"
#include "storage/yaml/yaml_defs.h"
#include "datastructs_screen.h"

class WidgetFactory;

class WidgetRefreshKey
{
 public:
  WidgetRefreshKey& add(uint32_t value)
  {
    mix(value);
    return *this;
  }

  WidgetRefreshKey& add(int32_t value)
  {
    return add((uint32_t)value);
  }

  WidgetRefreshKey& add(bool value)
  {
    return add((uint32_t)value);
  }

  WidgetRefreshKey& addBytes(const char* data, size_t len)
  {
    if (!data) return *this;
    for (size_t i = 0; i < len; i += 1) {
      state ^= (uint8_t)data[i];
      state *= FNV_PRIME;
    }
    return *this;
  }

  uint32_t value() const { return state; }

 private:
  static constexpr uint32_t FNV_OFFSET = 2166136261u;
  static constexpr uint32_t FNV_PRIME = 16777619u;

  uint32_t state = FNV_OFFSET;

  void mix(uint32_t value)
  {
    addBytes((const char*)&value, sizeof(value));
  }
};

//-----------------------------------------------------------------------------

struct MainViewWidgetLocation
{
  uint8_t screen;
  uint8_t zone;
};

struct TopBarWidgetLocation
{
  uint8_t zone;
};

class WidgetLocation
{
 public:
  // Explicit placement selects storage and interaction policy without sentinel
  // screen ids such as -1 for the top bar.
  enum class Placement : uint8_t {
    MainView,
    TopBar,
  };

  explicit WidgetLocation(MainViewWidgetLocation location) :
      placement_(Placement::MainView),
      screen_(location.screen),
      zone_(location.zone)
  {
  }

  explicit WidgetLocation(TopBarWidgetLocation location) :
      placement_(Placement::TopBar),
      screen_(0),
      zone_(location.zone)
  {
  }

  bool isMainView() const { return placement_ == Placement::MainView; }
  bool isTopBar() const { return placement_ == Placement::TopBar; }

  WidgetPersistentData* persistentData() const;

 private:
  Placement placement_;
  uint8_t screen_;
  uint8_t zone_;
};

//-----------------------------------------------------------------------------

struct WidgetOption
{
  // First two entries must match luaScriptInputType enum
  // TODO: should be cleaned up
  enum Type {
    Integer,
    Source,
    Bool,
    String,
    TextSize,
    Timer,
    Switch,
    Color,
    Align,
    Slider,
    Choice,
    File,
  };

  const char * name;
  Type type;
  WidgetOptionValue deflt;
  WidgetOptionValue min;
  WidgetOptionValue max;
  const char * displayName;
  std::string fileSelectPath;
#if !defined(BACKUP)
  std::vector<std::string> choiceValues;
#endif
};

//-----------------------------------------------------------------------------

class Widget : public ButtonBase
{
 public:

  Widget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
         WidgetLocation location);

  ~Widget() override = default;

  const WidgetFactory* getFactory() const { return factory; }

  const WidgetOption* getOptionDefinitions() const;
  bool hasOptions() const { return getOptionDefinitions() && getOptionDefinitions()->name; }
  bool isTopBarWidget() const { return location.isTopBar(); }
  bool isMainViewWidget() const { return location.isMainView(); }
  bool isCompactTopBarWidget() const
  {
    return isTopBarWidget() && height() <= EdgeTxStyles::MENU_HEADER_HEIGHT;
  }

  virtual const char* getErrorMessage() const { return nullptr; }

  WidgetPersistentData* getPersistentData();
  uint32_t getPersistentDataRevision();

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "Widget"; }
#endif

  // Window interface
#if defined(HARDWARE_KEYS)
  void onLiveEvent(LiveWindow& live, event_t event) override;
#endif

  // Widget interface

  // Set/unset fullscreen mode
  void setFullscreen(bool enable);
  void closeFullscreen() { closeFS = true; }
  bool isFullscreen() const { return fullscreen; }

  // Should rotary encoder events be enabled when full screen
  virtual bool enableFullScreenRE() const { return true; }

  // Called when the widget options have changed
  void updateWithoutRefresh();
  void update();

  // Called at regular time interval if the widget is hidden or off screen
  void background();
  // Called at regular time interval if the widget is visible and on screen
  void foreground();

  // Update widget 'zone' data (for Lua widgets)
  virtual void updateZoneRect(rect_t rect, bool updateUI = true)
  {
    if (updateUI) update();
  }

  void enableFocus(bool enable);

 protected:
  const WidgetFactory* factory;
  WidgetLocation location;
  bool fullscreen = false;
  bool closeFS = false;
  lv_obj_t* focusBorder = nullptr;
  lv_style_t borderStyle;
  lv_point_t borderPts[5];

  static FontIndex responsiveTextFont(coord_t height);
  static void layoutTextLabel(lv_obj_t* label, const rect_t& rect,
                              FontIndex font, coord_t xOffset = 0,
                              coord_t yOffset = 0);

  void onCancel() override;
  bool onLiveLongPress(LiveWindow&) override;

  virtual void onUpdateWithoutRefresh();
  virtual void onUpdate() {}
  virtual void onBackground() {}
  virtual void onForeground() {}
  virtual void onFullscreen(bool enable) {}
  void openMenu();
  void delayWidgetLoad();

  template <typename Fn>
  bool runWidgetTask(Fn&& fn)
  {
    return visitLive([&](LiveWindow&) {
      if (taskRequiresLoaded && !loaded) return false;
      fn();
      return true;
    });
  }

 protected:
  bool taskRequiresLoaded = false;
};

//-----------------------------------------------------------------------------

class TrackedWidget : public Widget
{
 public:
  enum class LoadMode {
    Immediate,
    Delayed
  };

  TrackedWidget(const WidgetFactory* factory, Window* parent,
                const rect_t& rect, WidgetLocation location,
                LoadMode loadMode);

 protected:
  void requireRefresh() { refreshPending = true; }

  virtual uint32_t refreshKey() = 0;
  virtual void refresh() = 0;
  void onForeground() final;

 private:
  uint32_t lastRefreshKey = 0;
  bool refreshPending = true;
};

//-----------------------------------------------------------------------------

class WidgetFactory
{
 public:
  using RegisteredWidgets =
      std::vector<std::reference_wrapper<const WidgetFactory>>;

  explicit WidgetFactory(const char* name, const WidgetOption* options = nullptr,
                         const char* displayName = nullptr) :
      name(name), displayName(displayName), options(options)
  {
    registerWidget(*this);
  }

  virtual ~WidgetFactory();

  const char* getName() const { return name; }

  const WidgetOption* getDefaultOptions() const { return options; }
  virtual const void parseOptionDefaults() const {}
  virtual const void checkOptions(const WidgetLocation& location) const {}

  const char* getDisplayName() const
  {
    return displayName ? displayName : name;
  }

  Widget* create(Window* parent, const rect_t& rect,
                 WidgetLocation location, bool init = true) const;

  virtual Widget* createNew(Window* parent, const rect_t& rect,
                            WidgetLocation location) const = 0;

  virtual bool isLuaWidgetFactory() const { return false; }

  static const RegisteredWidgets& getRegisteredWidgets();
  static const WidgetFactory* getWidgetFactory(const char* name);
  static Widget* newWidget(const char* name, Window* parent, const rect_t& rect,
                           WidgetLocation location);

 protected:
  const char* name = nullptr;
  const char* displayName = nullptr;
  const WidgetOption* options = nullptr;

 private:
  static RegisteredWidgets& registeredWidgets();
  static void registerWidget(const WidgetFactory& factory);
  static void unregisterWidget(const WidgetFactory& factory);
};

//-----------------------------------------------------------------------------

template <class T>
class BaseWidgetFactory : public WidgetFactory
{
 public:
  BaseWidgetFactory(const char* name, const WidgetOption* options,
                    const char* displayName = nullptr) :
      WidgetFactory(name, options, displayName)
  {
  }

  Widget* createNew(Window* parent, const rect_t& rect,
                    WidgetLocation location) const override
  {
    return new (std::nothrow) T(this, parent, rect, location);
  }
};

//-----------------------------------------------------------------------------
