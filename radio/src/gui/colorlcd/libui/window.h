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

#pragma once

#include <functional>
#include <list>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

#include "definitions.h"
#include "edgetx_helpers.h"
#include "etx_lv_theme.h"
#include "messaging.h"

class Window;
class FlexGridLayout;
class FormLine;
class MainWindow;
class PageGroup;

typedef uint32_t WindowFlags;

typedef lv_obj_t* (*LvglCreate)(lv_obj_t*);

#if !defined(_GNUC_)
#undef OPAQUE
#endif

constexpr WindowFlags OPAQUE = 1u << 0u;
constexpr WindowFlags NO_FOCUS = 1u << 1u;
constexpr WindowFlags NO_SCROLL = 1u << 2u;
constexpr WindowFlags NO_CLICK = 1u << 3u;
constexpr WindowFlags NO_FORCED_SCROLL = 1u << 4u;

//-----------------------------------------------------------------------------

class LvObjHandle
{
 public:
  bool isPresent() const { return obj_ != nullptr; }
  bool isPresentForTest() const { return isPresent(); }
  lv_obj_t* getForTest() const { return obj_; }

  template <typename Fn>
  bool with(Fn&& fn) const
  {
    if (!obj_) return false;
    using Result = std::invoke_result_t<Fn, lv_obj_t*>;
    if constexpr (std::is_void_v<Result>) {
      fn(obj_);
      return true;
    } else {
      return static_cast<bool>(fn(obj_));
    }
  }

 protected:
  void reset(lv_obj_t* obj) { obj_ = obj; }

 private:
  lv_obj_t* obj_ = nullptr;
};

class RequiredLvObj : public LvObjHandle
{
 private:
  friend class Window;

  using LvObjHandle::reset;
};

class OptionalLvObj : public LvObjHandle
{
 public:
  using LvObjHandle::reset;
};

//-----------------------------------------------------------------------------

template <typename T>
class WindowHandle
{
 public:
  bool isPresent() const { return window_ != nullptr; }
  bool isPresentForTest() const { return isPresent(); }
  T* getForTest() const { return window_; }

  template <typename Fn>
  bool with(Fn&& fn) const
  {
    if (!window_) return false;
    using Result = std::invoke_result_t<Fn, T&>;
    if constexpr (std::is_void_v<Result>) {
      fn(*window_);
      return true;
    } else {
      return static_cast<bool>(fn(*window_));
    }
  }

  template <typename Fn>
  bool withLive(Fn&& fn) const;

  template <typename Value, typename Fn>
  Value valueOr(Value missing, Fn&& fn) const
  {
    if (!window_) return missing;
    return fn(*window_);
  }

 protected:
  void reset(T* window) { window_ = window; }

 private:
  T* window_ = nullptr;
};

template <typename T>
class RequiredWindow : public WindowHandle<T>
{
 private:
  friend class Window;

  using WindowHandle<T>::reset;
};

template <typename T>
class OptionalWindow : public WindowHandle<T>
{
 public:
  using WindowHandle<T>::reset;
};

//-----------------------------------------------------------------------------

class Window
{
 private:
  enum class Availability {
    Available,
    Unavailable,
  };

 public:
  class LiveWindow
  {
   public:
    Window& window() const { return window_; }
    lv_obj_t* lvobj() const { return lvobj_; }

   private:
    friend class Window;

    LiveWindow(Window& window, lv_obj_t* lvobj) : window_(window), lvobj_(lvobj)
    {
    }

    Window& window_;
    lv_obj_t* lvobj_;
  };

  class LoadedWindow
  {
   public:
    Window& window() const { return window_; }
    lv_obj_t* lvobj() const { return lvobj_; }

   private:
    friend class Window;

    explicit LoadedWindow(LiveWindow& live) :
        window_(live.window()),
        lvobj_(live.lvobj())
    {
    }

    Window& window_;
    lv_obj_t* lvobj_;
  };

  Window(const rect_t& rect);
  Window(Window* parent, const rect_t& rect, LvglCreate objConstruct = nullptr);

  virtual ~Window();

  static Window* fromLvObj(lv_obj_t* obj);
  static Window* fromAvailableLvObj(lv_obj_t* obj);

  template <typename T>
  static T* adoptLive(T* window)
  {
    if (window && !window->withLive([](LiveWindow&) {})) {
      window->detach();
      delete window;
      return nullptr;
    }
    return window;
  }

  template <typename T, typename... Args>
  static T* makeLive(Args&&... args)
  {
    return adoptLive(new (std::nothrow) T(std::forward<Args>(args)...));
  }

#if defined(DEBUG_WINDOWS)
  virtual std::string getName() const;
  std::string getRectString() const;
  std::string getIndentString() const;
  std::string getWindowDebugString(const char* name = nullptr) const;
#endif

  Window* getParent() const { return parent; }

  Window* getFullScreenWindow();

  void setWindowFlag(WindowFlags flag);
  void clearWindowFlag(WindowFlags flag);
  bool hasWindowFlag(WindowFlags flag) const { return windowFlags & flag; }

  void setTextFlag(LcdFlags flag);
  void clearTextFlag(LcdFlags flag);
  LcdFlags getTextFlags() const { return textFlags; }

  typedef std::function<void()> CloseHandler;
  void setCloseHandler(CloseHandler h) { closeHandler = std::move(h); }

  typedef std::function<void(bool)> FocusHandler;
  void setFocusHandler(FocusHandler h) { focusHandler = std::move(h); }

  typedef std::function<void(coord_t, coord_t)> ScrollHandler;
  void setScrollHandler(ScrollHandler h) { scrollHandler = std::move(h); }

  virtual void clear();
  void deleteLater();

  bool hasFocus() const;
  bool focus();

  void setRect(rect_t value)
  {
    rect = value;
    if (!lvobj) return;
    lv_obj_set_pos(lvobj, rect.x, rect.y);
    lv_obj_set_size(lvobj, rect.w, rect.h);
  }

  void setWidth(coord_t value)
  {
    rect.w = value;
    if (!lvobj) return;
    lv_obj_set_width(lvobj, rect.w);
  }

  void setHeight(coord_t value)
  {
    rect.h = value;
    if (!lvobj) return;
    lv_obj_set_height(lvobj, rect.h);
  }

  void setTop(coord_t y)
  {
    rect.y = y;
    if (!lvobj) return;
    lv_obj_set_pos(lvobj, rect.x, rect.y);
  }

  void setSize(coord_t w, coord_t h)
  {
    rect.w = w;
    rect.h = h;
    if (!lvobj) return;
    lv_obj_set_size(lvobj, w, h);
  }

  void setPos(coord_t x, coord_t y)
  {
    rect.x = x;
    rect.y = y;
    if (!lvobj) return;
    lv_obj_set_pos(lvobj, x, y);
  }

  coord_t left() const { return rect.x; }
  coord_t right() const { return rect.x + rect.w; }
  coord_t top() const { return rect.y; }
  coord_t bottom() const { return rect.y + rect.h; }

  coord_t width() const { return rect.w; }
  coord_t height() const { return rect.h; }

  rect_t getRect() const { return rect; }
  uint32_t getLvIndex() const;
  coord_t getScrollY() const;

  void padLeft(coord_t pad);
  void padRight(coord_t pad);
  void padTop(coord_t pad);
  void padBottom(coord_t pad);
  void padAll(PaddingSize pad);
  void addFlag(lv_obj_flag_t flag);
  void clearFlag(lv_obj_flag_t flag);
  bool hasFlag(lv_obj_flag_t flag) const;
  void addState(lv_state_t state);
  void clearState(lv_state_t state);
  bool hasState(lv_state_t state) const;
  void addStyle(lv_style_t* style, lv_style_selector_t selector);
  void addStyle(const lv_style_t& style, lv_style_selector_t selector);
  void updateLayout();
  void refreshStyle(lv_part_t part = LV_PART_ANY,
                    lv_style_prop_t prop = LV_STYLE_PROP_ANY);
  void setStyleMaxWidth(lv_coord_t value, lv_style_selector_t selector);
  void setStyleMaxHeight(lv_coord_t value, lv_style_selector_t selector);
  void setStyleFlexCrossPlace(lv_flex_align_t align,
                              lv_style_selector_t selector);
  void setStyleFlexMainPlace(lv_flex_align_t align,
                             lv_style_selector_t selector);
  void setStylePadColumn(lv_coord_t value, lv_style_selector_t selector);
  void setStyleGridCellXAlign(lv_grid_align_t align,
                              lv_style_selector_t selector);
  void setStyleGridCellYAlign(lv_grid_align_t align,
                              lv_style_selector_t selector);
  void setLayout(uint32_t layout);
  void setGridDscArray(const lv_coord_t* colDsc, const lv_coord_t* rowDsc);
  void setGridCell(lv_grid_align_t x_align, lv_coord_t col_pos,
                   lv_coord_t col_span, lv_grid_align_t y_align,
                   lv_coord_t row_pos, lv_coord_t row_span);
  void setGridAlign(lv_grid_align_t column_align, lv_grid_align_t row_align);
  void setAlign(lv_align_t align);
  void align(lv_align_t align, lv_coord_t xOfs = 0, lv_coord_t yOfs = 0);
  void setFlexFlow(lv_flex_flow_t flow);
  void setFlexGrow(uint8_t grow);
  void setFlexAlign(lv_flex_align_t main_place, lv_flex_align_t cross_place,
                    lv_flex_align_t track_cross_place);
  void setStylePadRow(lv_coord_t value, lv_style_selector_t selector);
  void addToGroup(lv_group_t* group);
  void removeFromGroup();
  void center();
  void moveForeground();
  void moveBackground();
  void scrollToY(coord_t y, lv_anim_enable_t anim);
  void scrollbar();
  void bgColor(LcdColorIndex color, lv_style_selector_t selector = LV_PART_MAIN);
  void solidBg();
  void solidBg(LcdColorIndex color, lv_style_selector_t selector = LV_PART_MAIN);
  void textColor(LcdColorIndex color, lv_style_selector_t selector = LV_PART_MAIN);
  void font(FontIndex fontIndex, lv_style_selector_t selector = LV_PART_MAIN);

  void onEvent(event_t event);
  void onClicked();
  virtual void onCancel();
  bool onLongPress();
  void dispatchKeyboardEvent(event_t event);
  bool sendLvEvent(lv_event_code_t code, void* param = nullptr);
  void addLvEventCb(lv_event_cb_t eventCb, lv_event_code_t filter,
                    void* userData);
  void checkEvents();

#if defined(SIMU)
  void setAutomationId(std::string value);
  void setAutomationRole(std::string value);
  void setAutomationText(std::string value);

  const std::string& automationId() const { return automationId_; }
  virtual std::string automationRole() const;
  virtual std::string automationText() const;
  virtual bool automationClickable() const;
  virtual bool automationLongClickable() const { return false; }
#else
  void setAutomationId(const char*) {}
  void setAutomationId(const std::string&) {}
  void setAutomationRole(const char*) {}
  void setAutomationRole(const std::string&) {}
  void setAutomationText(const char*) {}
  void setAutomationText(const std::string&) {}
#endif

  void invalidate();

  [[nodiscard]] bool attachTo(Window& window);

  void detach();

  bool isAvailable() const { return availability == Availability::Available; }
  bool hasLiveLvObj() const { return !_deleted && lvobj; }
  bool acceptsEvents() const { return isAvailable() && hasLiveLvObj(); }
  bool loadLvglScreen();

  template <typename Fn>
  bool withLive(Fn&& handler)
  {
    return withLiveImpl(*this, std::forward<Fn>(handler));
  }

  template <typename Fn>
  bool withLive(Fn&& handler) const
  {
    return withLiveImpl(*this, std::forward<Fn>(handler));
  }

#if defined(HARDWARE_TOUCH)
  void addBackButton();
  void addCustomButton(coord_t x, coord_t y, std::function<void()> action,
                       const char* automationId = nullptr,
                       const char* automationText = nullptr);
#endif

#if defined(SIMU)
  lv_obj_t* getLvObjForTest() const { return lvobj; }
#endif

  virtual bool isTopBar() { return false; }
  virtual bool isNavWindow() { return false; }
  virtual bool isPageGroup() { return false; }
  virtual bool isBubblePopup() { return false; }
  virtual const char* getFormFieldTitle() const { return nullptr; }

  void setFlexLayout(lv_flex_flow_t flow = LV_FLEX_FLOW_COLUMN,
                     lv_coord_t padding = PAD_TINY, coord_t width = LV_PCT(100),
                     coord_t height = LV_SIZE_CONTENT);
  FormLine* newLine(FlexGridLayout& layout);

  void show(bool visible = true);
  void hide() { show(false); }
  bool isVisible();
  bool isOnScreen();
  virtual void enable(bool enabled = true);
  void disable() { enable(false); }

  void pushLayer(bool hideParent = false);
  void popLayer();
  static Window* topWindow();
  static Window* firstOpaque();
  static PageGroup* pageGroup();

  void assignLvGroup(lv_group_t* g, bool setDefault);

 private:
  static std::list<Window*> trash;

 protected:
  rect_t rect;

  Window* parent = nullptr;

 private:
  lv_obj_t* lvobj = nullptr;

 protected:
  std::list<Window*> children;

  WindowFlags windowFlags = 0;
  LcdFlags textFlags = 0;

 private:
  friend class MainWindow;

  bool deleted() const { return _deleted; }

  bool _deleted = false;
  Availability availability = Availability::Available;
  bool longPressHandled = false;

 private:
  bool loaded = true;

 protected:
  bool layerCreated = false;
  bool parentHidden = false;

  CloseHandler closeHandler;
  FocusHandler focusHandler;
  ScrollHandler scrollHandler;

#if defined(SIMU)
  std::string automationId_;
  std::string automationRole_;
  std::string automationText_;
#endif

  void deleteChildren();

  virtual bool addChild(Window* window);
  virtual void onDelete() {}
  virtual void onDeleted() {}
  virtual void onLiveShow(LiveWindow& live, bool visible);
  virtual void onLiveEvent(LiveWindow& live, event_t event);
  virtual void onLiveClicked(LiveWindow& live);
  virtual bool onLiveLongPress(LiveWindow& live);
  virtual void onLiveCheckEvents(LiveWindow& live);

  bool requireLvObj(lv_obj_t* obj);
  bool requireLvGroup(lv_group_t* group)
  {
    if (group) return true;
    failClosed();
    return false;
  }
  bool requireLvObj(RequiredLvObj& target, lv_obj_t* obj)
  {
    if (!requireLvObj(obj)) return false;
    target.reset(obj);
    return true;
  }
  void failClosed();
  bool syncOverlay(Window* overlay);
  void markLoaded();

  template <typename Fn>
  bool runWhenLoaded(Fn&& handler)
  {
    return withLive([&](LiveWindow& live) {
      if (!loaded) return false;
      LoadedWindow loadedWindow(live);
      return invokeLoadedHandlerWith(loadedWindow, std::forward<Fn>(handler));
    });
  }

  template <typename Create, typename Init>
  bool initRequiredLvObj(lv_obj_t*& target, Create&& create, Init&& init)
  {
    return withLive([&](LiveWindow& live) {
      target = create(live.lvobj());
      if (!requireLvObj(target)) return false;
      init(target);
      return true;
    });
  }

  template <typename Create, typename Init>
  bool initRequiredLvObj(RequiredLvObj& target, Create&& create, Init&& init)
  {
    return withLive([&](LiveWindow& live) {
      lv_obj_t* obj = create(live.lvobj());
      if (!requireLvObj(obj)) return false;
      target.reset(obj);
      init(obj);
      return true;
    });
  }

  template <typename T, typename... Args>
  bool initRequiredWindow(T*& target, Args&&... args)
  {
    target = Window::makeLive<T>(std::forward<Args>(args)...);
    if (target) return true;
    failClosed();
    return false;
  }

  template <typename T, typename... Args>
  bool initRequiredWindow(RequiredWindow<T>& target, Args&&... args)
  {
    auto window = Window::makeLive<T>(std::forward<Args>(args)...);
    if (!window) {
      failClosed();
      return false;
    }
    target.reset(window);
    return true;
  }

  template <typename T>
  bool requireWindow(RequiredWindow<T>& target, T* window)
  {
    if (!window) {
      failClosed();
      return false;
    }
    target.reset(window);
    return true;
  }

  template <typename Stored, typename Actual, typename... Args>
  bool initRequiredWindowAs(RequiredWindow<Stored>& target, Args&&... args)
  {
    static_assert(std::is_base_of_v<Stored, Actual>,
                  "Actual window must derive from stored window type");
    auto window = Window::makeLive<Actual>(std::forward<Args>(args)...);
    if (!window) {
      failClosed();
      return false;
    }
    target.reset(window);
    return true;
  }

  template <typename T, typename Init, typename... Args>
  bool buildRequiredWindow(Init&& init, Args&&... args)
  {
    if (!isAvailable()) return false;

    RequiredWindow<T> window;
    if (!initRequiredWindow(window, std::forward<Args>(args)...)) return false;

    const bool initialized = window.with(std::forward<Init>(init));
    return initialized && isAvailable();
  }

  void eventHandler(lv_event_t* e);
  static void window_event_cb(lv_event_t* e);

  template <typename Fn>
  static constexpr bool LiveHandlerInvocable =
      std::is_invocable_v<Fn, LiveWindow&>;

  template <typename Fn>
  static constexpr bool LoadedHandlerInvocable =
      std::is_invocable_v<Fn, LoadedWindow&> || std::is_invocable_v<Fn>;

  template <typename Arg, typename Fn>
  static bool invokeLiveHandlerWith(Arg&& arg, Fn&& handler)
  {
    using Result = std::invoke_result_t<Fn, Arg>;
    if constexpr (std::is_void_v<Result>) {
      handler(std::forward<Arg>(arg));
      return true;
    } else {
      return static_cast<bool>(handler(std::forward<Arg>(arg)));
    }
  }

  template <typename Self, typename Fn>
  static bool withLiveImpl(Self& self, Fn&& handler)
  {
    if (!self.acceptsEvents()) return false;
    LiveWindow live(const_cast<Window&>(self), self.lvobj);
    static_assert(LiveHandlerInvocable<Fn>,
                  "withLive handler must accept Window::LiveWindow&");
    return invokeLiveHandlerWith(live, std::forward<Fn>(handler));
  }

  template <typename Fn>
  static bool invokeLoadedHandlerWith(LoadedWindow& loaded, Fn&& handler)
  {
    static_assert(LoadedHandlerInvocable<Fn>,
                  "runWhenLoaded handler must accept Window::LoadedWindow& "
                  "or no arguments");
    if constexpr (std::is_invocable_v<Fn, LoadedWindow&>) {
      using Result = std::invoke_result_t<Fn, LoadedWindow&>;
      if constexpr (std::is_void_v<Result>) {
        handler(loaded);
        return true;
      } else {
        return static_cast<bool>(handler(loaded));
      }
    } else {
      using Result = std::invoke_result_t<Fn>;
      if constexpr (std::is_void_v<Result>) {
        handler();
        return true;
      } else {
        return static_cast<bool>(handler());
      }
    }
  }
  virtual bool onLiveCustomEvent(LiveWindow& live, lv_event_t* event)
  {
    return false;
  }

  static void delayLoader(lv_event_t* e);
  void delayLoad();
  virtual void delayedInit() {}

#if defined(SIMU)
  bool loadedForTest() const { return loaded; }
#endif
};

template <typename T>
template <typename Fn>
bool WindowHandle<T>::withLive(Fn&& fn) const
{
  if (!window_) return false;
  return window_->withLive(std::forward<Fn>(fn));
}

//-----------------------------------------------------------------------------

class NavWindow : public Window
{
 public:
  NavWindow(Window* parent, const rect_t& rect,
            LvglCreate objConstruct = nullptr);

  bool isNavWindow() override { return true; }

#if defined(HARDWARE_KEYS)
  virtual void onPressSYS() {}
  virtual void onLongPressSYS() {}
  virtual void onPressMDL() {}
  virtual void onLongPressMDL() {}
  virtual void onPressTELE() {}
  virtual void onLongPressTELE() {}
  virtual void onPressPGUP() {}
  virtual void onPressPGDN() {}
  virtual void onLongPressPGUP() {}
  virtual void onLongPressPGDN() {}
  virtual void onLongPressRTN() {}
#endif

 protected:
  virtual bool bubbleEvents() { return true; }
  void onLiveEvent(LiveWindow& live, event_t event) override;
};

struct PageButtonDef {
  STR_TYP title;
  std::function<void()> createPage;
  std::function<bool()> isActive;
  std::function<bool()> enabled;
};

//-----------------------------------------------------------------------------

class SetupButtonGroup : public Window
{
 public:
  SetupButtonGroup(Window* parent, const rect_t& rect, const char* title,
                   int cols, PaddingSize padding, const PageButtonDef* pages,
                   coord_t btnHeight = EdgeTxStyles::UI_ELEMENT_HEIGHT);

 protected:
};

//-----------------------------------------------------------------------------

class SetupLine;

struct SetupLineDef {
  STR_TYP title;
  std::function<void(SetupLine*, coord_t, coord_t)> createEdit;
};

class SetupLine : public Window
{
 public:
  SetupLine(Window* parent, coord_t y, coord_t col2, PaddingSize padding,
            const char* title,
            std::function<void(SetupLine*, coord_t, coord_t)> createEdit,
            coord_t lblYOffset = 0);
  const std::string& getTitle() const { return titleText; }
  const char* getFormFieldTitle() const override { return titleText.c_str(); }

  static coord_t showLines(Window* parent, coord_t y, coord_t col2,
                           PaddingSize padding, const SetupLineDef* setupLines);

  Messaging setupMsg;

 protected:
  std::string titleText;
};

//-----------------------------------------------------------------------------
