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
#include <utility>
#include <vector>

#include "button.h"
#include "storage/yaml/yaml_defs.h"
#include "datastructs_screen.h"
#include "widgets_container.h"

class WidgetFactory;

class WidgetRefreshKey
{
 public:
  WidgetRefreshKey& add(uint32_t value)
  {
    mix(value);
    return *this;
  }

  WidgetRefreshKey& add(int32_t value) { return add((uint32_t)value); }

  WidgetRefreshKey& add(bool value) { return add((uint32_t)value); }

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

  void mix(uint32_t value) { addBytes((const char*)&value, sizeof(value)); }
};

//-----------------------------------------------------------------------------

struct MainViewWidgetLocation {
  uint8_t screen;
  uint8_t zone;
};

struct TopBarWidgetLocation {
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
      placement_(Placement::TopBar), screen_(0), zone_(location.zone)
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

struct WidgetOption {
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

  const char* name;
  Type type;
  WidgetOptionValue deflt;
  WidgetOptionValue min;
  WidgetOptionValue max;
  const char* displayName;
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
  bool hasOptions() const
  {
    return getOptionDefinitions() && getOptionDefinitions()->name;
  }
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
  OptionalLvObj focusBorder;
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
    return runWhenLoaded(std::forward<Fn>(fn));
  }
};

//-----------------------------------------------------------------------------

class NativeWidget : public Widget
{
 public:
  NativeWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
               WidgetLocation location);

 protected:
  static constexpr coord_t CARD_RADIUS = 6;
  static constexpr coord_t PILL_RADIUS = 0;

  rect_t cardRect() const;
  rect_t contentRect() const;
  bool usesCardChrome() const;
  void invalidateNativeRefresh() { refreshPending = true; }

  static void setObjRect(lv_obj_t* obj, coord_t x, coord_t y, coord_t w,
                         coord_t h);
  static void setObjVisible(lv_obj_t* obj, bool visible);
  static lv_obj_t* createFlexBox(lv_obj_t* parent, lv_flex_flow_t flow);
  static void layoutFlexBox(lv_obj_t* obj, const rect_t& rect,
                            lv_flex_flow_t flow, coord_t gap = PAD_TINY,
                            lv_flex_align_t main = LV_FLEX_ALIGN_START,
                            lv_flex_align_t cross = LV_FLEX_ALIGN_CENTER,
                            lv_flex_align_t track = LV_FLEX_ALIGN_START);
  static void setFlexChild(lv_obj_t* obj, coord_t width, coord_t height,
                           uint8_t grow = 0);
  static FontIndex cardTitleFont(const rect_t& content);
  static FontIndex cardValueFont(const rect_t& content);
  static FontIndex cardStackTitleFont(const rect_t& content);
  static FontIndex cardStackValueFont(const rect_t& content);
  static coord_t cardHeaderHeight(const rect_t& content);
  static coord_t cardGap(const rect_t& content);
  static coord_t cardBarHeight(const rect_t& content);
  static void layoutCardHeader(lv_obj_t* row, lv_obj_t* title, lv_obj_t* value,
                               const rect_t& rect, coord_t valueWidth = 0);
  static void layoutCardStack(lv_obj_t* column, lv_obj_t* title,
                              lv_obj_t* value, const rect_t& rect);
  static rect_t insetRect(const rect_t& rect, coord_t inset);
  static FontIndex fitTextFont(const char* text, coord_t width, coord_t height,
                               const FontIndex* fonts, uint8_t fontCount);
  static void layoutText(lv_obj_t* obj, const rect_t& rect, FontIndex font,
                         lv_color_t color, lv_text_align_t align);
  static void layoutInlineMetric(lv_obj_t* label, lv_obj_t* value,
                                 const rect_t& rect, coord_t valueWidth,
                                 FontIndex font);
  static void layoutPillBar(lv_obj_t* track, lv_obj_t* fill, const rect_t& rect,
                            uint8_t percent, lv_color_t fillColor);
  static lv_color_t cardColor();
  static lv_color_t borderColor();
  static lv_color_t pillColor();
  static lv_color_t trackColor();
  static lv_color_t primaryTextColor();
  static lv_color_t mutedTextColor();

  virtual void createContent(lv_obj_t* parent) = 0;
  virtual void layoutContent(const rect_t& content) = 0;
  virtual uint32_t contentRefreshKey() { return 0; }
  virtual void refreshContent() {}

 private:
  RequiredLvObj card;
  uint32_t lastRefreshKey = 0;
  bool refreshPending = true;

  void delayedInit() final;
  void onUpdate() final;
  void onForeground() final;
};

//-----------------------------------------------------------------------------

class TrackedWidget : public Widget
{
 public:
  enum class LoadMode { Immediate, Delayed };

  TrackedWidget(const WidgetFactory* factory, Window* parent,
                const rect_t& rect, WidgetLocation location, LoadMode loadMode);

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

  explicit WidgetFactory(const char* name,
                         const WidgetOption* options = nullptr,
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

  Widget* create(Window* parent, const rect_t& rect, WidgetLocation location,
                 bool init = true) const;

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
