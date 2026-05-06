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

#include "pagegroup.h"

#include "etx_lv_theme.h"
#include "keyboard_base.h"
#include "mainwindow.h"
#include "model_select.h"
#include "os/time.h"
#include "radio_tools.h"
#include "screen_setup.h"
#include "theme_manager.h"
#include "topbar.h"
#include "view_channels.h"
#include "view_main.h"

#include <new>

#if defined(DEBUG)
static uint32_t dsms, dems, end_ms, start_ms;
static bool timepg = false;

static void on_draw_begin(lv_event_t* e)
{
  if (timepg) {
    dsms = time_get_ms();
  }
}
static void on_draw_end(lv_event_t* e)
{
  timepg = false;
  dems = time_get_ms();
  TRACE("tab time: build %ld layout %ld draw %ld total %ld",
        end_ms - start_ms, dsms - end_ms, dems - dsms, dems - start_ms);
}
#endif

//-----------------------------------------------------------------------------

#if VERSION_MAJOR == 2
class SelectedTabIcon : public StaticIcon
{
 public:
  SelectedTabIcon(Window* parent) :
      StaticIcon(parent, 0, 0, ICON_CURRENTMENU_SHADOW, COLOR_THEME_PRIMARY1_INDEX)
  {
    new StaticIcon(this, 0, 0, ICON_CURRENTMENU_BG, COLOR_THEME_FOCUS_INDEX);
    new StaticIcon(this, SEL_DOT_X, SEL_DOT_Y, ICON_CURRENTMENU_DOT, COLOR_THEME_PRIMARY2_INDEX);
  }

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "SelectedTabIcon"; }
#endif

  static LAYOUT_VAL_SCALED(SEL_DOT_X, 10)
  static LAYOUT_VAL_SCALED(SEL_DOT_Y, 39)
};

class PageGroupIconButton : public ButtonBase
{
 public:
  PageGroupIconButton(Window* parent, const rect_t& rect, PageGroupItem* page, int idx) :
      ButtonBase(parent, rect, nullptr, window_create), pageTab(page), index(idx)
  {
    new StaticIcon(this, 2, ICON_Y, pageTab->getIcon(), COLOR_THEME_PRIMARY2_INDEX);
    show(isVisible());
  }

#if defined(DEBUG_WINDOWS)
  std::string getName() const override
  {
    return "PageGroupIconButton(" + std::to_string(index) + ")";
  }
#endif

  bool isVisible() const { return pageTab->isVisible(); }

  static LAYOUT_VAL_SCALED(ICON_Y, 7)

 protected:
  PageGroupItem* pageTab;
  int index;

  void onLiveCheckEvents(LiveWindow& live) override
  {
    show(isVisible());
    ButtonBase::onLiveCheckEvents(live);
  }
};
#endif

//-----------------------------------------------------------------------------

PageGroupHeaderBase::PageGroupHeaderBase(Window* parent, coord_t height, EdgeTxIcon icon, const char* parentTitle, PageGroupBase* menu) :
    Window(parent, {0, 0, LCD_W, height}), menu(menu)
{
    solidBg(COLOR_THEME_SECONDARY1_INDEX);

    hdrIcon = new (std::nothrow) HeaderIcon(this, icon);

#if VERSION_MAJOR > 2
    new (std::nothrow) HeaderBackIcon(this);

    withLive([&](LiveWindow& live) {
      auto obj = etx_label_create(live.lvobj());
      if (!obj) return;
      parentLabel.reset(obj);
      etx_txt_color(obj, COLOR_THEME_PRIMARY2_INDEX);
      lv_obj_set_pos(obj, PageHeader::PAGE_TITLE_LEFT, PageHeader::PAGE_TITLE_TOP);
      lv_obj_set_size(obj,
                      LCD_W - PageHeader::PAGE_TITLE_LEFT -
                          PageGroup::PAGE_GROUP_BACK_BTN_W * 2 - PAD_LARGE * 2,
                      EdgeTxStyles::STD_FONT_HEIGHT);
      lv_label_set_text(obj, parentTitle);
    });
#endif

    initRequiredLvObj(
        titleLabel, [](lv_obj_t* parent) { return etx_label_create(parent); },
        [](lv_obj_t* obj) { etx_txt_color(obj, COLOR_THEME_PRIMARY2_INDEX); });

#if VERSION_MAJOR == 2
    withLive([&](LiveWindow& live) {
      auto sep = lv_obj_create(live.lvobj());
      if (!requireLvObj(sep)) return false;
      etx_solid_bg(sep);
      lv_obj_set_pos(sep, 0, EdgeTxStyles::MENU_HEADER_HEIGHT);
      lv_obj_set_size(sep, LCD_W,
                      PageGroup::PAGE_GROUP_TOP_BAR_H -
                          EdgeTxStyles::MENU_HEADER_HEIGHT);
      return true;
    });

    titleLabel.with([](lv_obj_t* obj) {
      lv_obj_set_style_pad_left(obj, PAD_MEDIUM, LV_PART_MAIN);
      lv_obj_set_style_pad_top(obj, 1, LV_PART_MAIN);
      lv_obj_set_pos(obj, 0, PageGroup::PAGE_GROUP_TOP_BAR_H);
      lv_obj_set_size(obj, LCD_W, PageGroup::PAGE_GROUP_ALT_TITLE_H);
    });
#else
    titleLabel.with([](lv_obj_t* obj) {
      lv_obj_set_pos(obj, PageHeader::PAGE_TITLE_LEFT,
                     PageHeader::PAGE_TITLE_TOP + EdgeTxStyles::STD_FONT_HEIGHT);
      lv_obj_set_size(obj,
                      LCD_W - PageHeader::PAGE_TITLE_LEFT -
                          PageGroup::PAGE_GROUP_BACK_BTN_W * 2 - PAD_LARGE * 2,
                      EdgeTxStyles::STD_FONT_HEIGHT);
    });
#endif

    setTitle("");

#if VERSION_MAJOR == 2
    carousel = new (std::nothrow) Window(this,
                                         {MENU_HEADER_BUTTONS_LEFT, 0,
                                          LCD_W - MENU_HEADER_BUTTONS_LEFT, EdgeTxStyles::MENU_HEADER_HEIGHT + ICON_EXTRA_H});
    if (!carousel) return;
    carousel->padAll(PAD_ZERO);
    carousel->setWindowFlag(NO_FOCUS);

    selectedIcon = new (std::nothrow) SelectedTabIcon(carousel);
#endif
}

#if VERSION_MAJOR == 2
coord_t PageGroupHeaderBase::getX(uint8_t idx)
{
  coord_t x = 0;
  for (uint8_t i = 0; i < idx; i += 1)
    if (buttons[i]->isVisible())
      x += MENU_HEADER_BUTTON_WIDTH;
  return x;
}
#endif

void PageGroupHeaderBase::setCurrentIndex(uint8_t index)
{
  if (index < pages.size()) {
    currentIndex = index;
#if VERSION_MAJOR == 2
    if (index < buttons.size()) {
      buttons[currentIndex]->check(false);
      currentIndex = index;
      buttons[currentIndex]->check(true);
      coord_t x = getX(currentIndex);
      selectedIcon->setPos(x, 0);
      coord_t sx = 0;
      carousel->withLive([&](Window::LiveWindow& live) {
        sx = lv_obj_get_scroll_x(live.lvobj());
      });
      if (x + MENU_HEADER_BUTTON_WIDTH - sx > carousel->width()) {
        carousel->withLive([&](Window::LiveWindow& live) {
          lv_obj_scroll_to(live.lvobj(),
                           x + MENU_HEADER_BUTTON_WIDTH - carousel->width(), 0,
                           LV_ANIM_OFF);
        });
      } else if (x < sx) {
        carousel->withLive([&](Window::LiveWindow& live) {
          lv_obj_scroll_to(live.lvobj(), x, 0, LV_ANIM_OFF);
        });
      }
    }
#endif
  }
}

void PageGroupHeaderBase::setTitle(const char* title)
{
#if VERSION_MAJOR == 2
  std::string s = replaceAll(title, "\n", " ");
  titleLabel.with([&](lv_obj_t* obj) { lv_label_set_text(obj, s.c_str()); });
#else
  titleLabel.with([&](lv_obj_t* obj) { lv_label_set_text(obj, title); });
#endif
}

void PageGroupHeaderBase::setIcon(EdgeTxIcon newIcon)
{
  if (hdrIcon) hdrIcon->setIcon(newIcon);
}

void PageGroupHeaderBase::addTab(PageGroupItem* page)
{
  if (!page) return;
  pages.emplace_back(page);

#if VERSION_MAJOR == 2
  uint8_t idx = buttons.size();
  auto btn = new (std::nothrow) PageGroupIconButton(
      carousel, {getX(idx), 0, MENU_HEADER_BUTTON_WIDTH + PAD_THREE, PageGroup::PAGE_GROUP_TOP_BAR_H + PAD_THREE + PAD_TINY}, page, idx);
  if (!btn) {
    pages.pop_back();
    return;
  }
  btn->setPressHandler([=]() {
    menu->setCurrentTab(idx);
    return true;
  });
  btn->show(btn->isVisible());
  buttons.emplace_back(btn);
#endif
}

bool PageGroupHeaderBase::hasSubMenu(QMPage qmPage)
{
  for (size_t i = 0; i < pages.size(); i += 1) {
    if (pages[i]->pageId() == qmPage)
      return true;
  }
  return false;
}

void PageGroupHeaderBase::onDelete()
{
  for (size_t i = 0; i < pages.size(); i += 1)
    delete pages[i];
  pages.clear();
}

#if VERSION_MAJOR == 2
void PageGroupHeaderBase::onLiveCheckEvents(Window::LiveWindow& live)
{
  for (uint8_t i = 0; i < buttons.size(); i += 1) {
    buttons[i]->setPos(getX(i), 0);
  }

  Window::onLiveCheckEvents(live);
}
#endif

//-----------------------------------------------------------------------------

class PageGroupHeader : public PageGroupHeaderBase
{
 public:
  PageGroupHeader(PageGroup* menu, EdgeTxIcon icon, const char* parentTitle) :
      PageGroupHeaderBase(menu, PageGroup::PAGE_GROUP_BODY_Y, icon, parentTitle, menu)
  {
  }

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "PageGroupHeader"; }
#endif

  void chgTab(int dir) override
  {
    int idx = currentIndex;
    do {
      idx += dir;
      if (idx < 0) idx = pages.size() - 1;
      if (idx >= (int)pages.size()) idx = 0;
    } while (!pages[idx]->isVisible());
    menu->setCurrentTab(idx);
  }

 protected:
};

//-----------------------------------------------------------------------------

PageGroupBase::PageGroupBase(coord_t bodyY, EdgeTxIcon icon) :
    NavWindow(MainWindow::instance(), {0, 0, LCD_W, LCD_H}), icon(icon)
{
  solidBg();

  pushLayer(true);

  initRequiredWindow(body, this, rect_t{0, bodyY, LCD_W, LCD_H - bodyY});
  body.with([&](Window& body) {
    body.setWindowFlag(NO_FOCUS);
    body.withLive([&](Window::LiveWindow& live) {
      lv_obj_set_style_max_height(live.lvobj(), LCD_H - bodyY, LV_PART_MAIN);
      etx_scrollbar(live.lvobj());
    });
  });

#if defined(DEBUG)
  withLive([](LiveWindow& live) {
    lv_obj_add_event_cb(live.lvobj(), on_draw_begin, LV_EVENT_COVER_CHECK,
                        nullptr);
    lv_obj_add_event_cb(live.lvobj(), on_draw_end, LV_EVENT_DRAW_POST_END,
                        nullptr);
  });
#endif

  quickMenuMsg.subscribe(Messaging::QUICK_MENU_ITEM_SELECT,
      [=](uint32_t param) {
        if (param)
          onCancel();
      });
}

void PageGroupBase::onLiveCheckEvents(Window::LiveWindow& live)
{
  NavWindow::onLiveCheckEvents(live);
  if (currentTab) {
    currentTab->checkEvents();
  }
}

void PageGroupBase::onLiveClicked(LiveWindow&) { Keyboard::hide(false); }

void PageGroupBase::onCancel()
{
  deleteLater();
}

uint8_t PageGroupBase::tabCount() const
{
  return header.valueOr<uint8_t>(0, [](PageGroupHeaderBase& header) {
    return header.tabCount();
  });
}

void PageGroupBase::addTab(PageGroupItem* page)
{
  header.with([&](PageGroupHeaderBase& header) { header.addTab(page); });
  if (!currentTab) {
    setCurrentTab(0);
  }
}

void PageGroupBase::setCurrentTab(unsigned index)
{
  withLive([&](LiveWindow&) {
    header.with([&](PageGroupHeaderBase& header) {
      body.with([&](Window& body) {
        header.setCurrentIndex(index);

        PageGroupItem* tab = header.pageTab(index);
        if (!tab) return;

        if (tab != currentTab) {
          header.setTitle(tab->getTitle().c_str());
#if VERSION_MAJOR > 2
          header.setIcon(tab->getIcon());
#endif

          if (isPageGroup())
            QuickMenu::setCurrentPage(tab->pageId(), icon);

          lv_obj_enable_style_refresh(false);

          body.clear();
          if (currentTab)
            currentTab->cleanup();
          currentTab = tab;

#if defined(DEBUG)
          start_ms = time_get_ms();
          timepg = true;
#endif

          static lv_style_prop_t remStyles[] = {
              LV_STYLE_FLEX_FLOW,  LV_STYLE_LAYOUT,    LV_STYLE_PAD_ROW,
              LV_STYLE_PAD_COLUMN, LV_STYLE_PAD_LEFT,  LV_STYLE_PAD_RIGHT,
              LV_STYLE_PAD_TOP,    LV_STYLE_PAD_BOTTOM};
          body.withLive([&](Window::LiveWindow& liveBody) {
            for (uint8_t i = 0; i < DIM(remStyles); i += 1)
              lv_obj_remove_local_style_prop(liveBody.lvobj(), remStyles[i],
                                             LV_PART_MAIN);
          });

          body.padAll(tab->getPadding());

          tab->build(&body);

          lv_obj_enable_style_refresh(true);
          body.withLive([](Window::LiveWindow& liveBody) {
            lv_obj_refresh_style(liveBody.lvobj(), LV_PART_ANY,
                                 LV_STYLE_PROP_ANY);
          });

#if defined(DEBUG)
          end_ms = time_get_ms();
#endif
        }
      });
    });
  });
}

#if defined(HARDWARE_KEYS)
void PageGroupBase::doKeyShortcut(event_t event)
{
  QMPage pg = g_eeGeneral.getKeyShortcut(event);
  if (pg == QM_APP) {
    onCancel();
    runLuaTool(g_eeGeneral.getKeyToolName(event));
  } else if (pg == QM_OPEN_QUICK_MENU) {
    QuickMenu::openQuickMenu();
  } else {
    if (QuickMenu::subMenuIcon(pg) == icon) {
      setCurrentTab(QuickMenu::pageIndex(pg));
    } else {
      onCancel();
      QuickMenu::openPage(pg);
    }
  }
}
void PageGroupBase::onPressSYS() { doKeyShortcut(EVT_KEY_BREAK(KEY_SYS)); }
void PageGroupBase::onLongPressSYS() { doKeyShortcut(EVT_KEY_LONG(KEY_SYS)); }
void PageGroupBase::onPressMDL() { doKeyShortcut(EVT_KEY_BREAK(KEY_MODEL)); }
void PageGroupBase::onLongPressMDL() { doKeyShortcut(EVT_KEY_LONG(KEY_MODEL)); }
void PageGroupBase::onPressTELE() { doKeyShortcut(EVT_KEY_BREAK(KEY_TELE)); }
void PageGroupBase::onLongPressTELE() { doKeyShortcut(EVT_KEY_LONG(KEY_TELE)); }

void PageGroupBase::onPressPGUP()
{
  header.with([](PageGroupHeaderBase& header) { header.prevTab(); });
}
void PageGroupBase::onPressPGDN()
{
  header.with([](PageGroupHeaderBase& header) { header.nextTab(); });
}
void PageGroupBase::onLongPressPGUP()
{
  header.with([](PageGroupHeaderBase& header) { header.prevTab(); });
}
void PageGroupBase::onLongPressPGDN()
{
  header.with([](PageGroupHeaderBase& header) { header.nextTab(); });
}
void PageGroupBase::onLongPressRTN() { onCancel(); }
#endif

bool PageGroupBase::hasSubMenu(QMPage qmPage)
{
  return header.valueOr(false, [&](PageGroupHeaderBase& header) {
    return header.hasSubMenu(qmPage);
  });
}

coord_t PageGroupBase::getScrollY()
{
  return body.valueOr<coord_t>(0, [](Window& body) {
    coord_t scrollY = 0;
    body.withLive([&](Window::LiveWindow& live) {
      scrollY = lv_obj_get_scroll_y(live.lvobj());
    });
    return scrollY;
  });
}

void PageGroupBase::setScrollY(coord_t y)
{
  body.with([&](Window& body) {
    body.withLive([&](Window::LiveWindow& live) {
      lv_obj_scroll_to_y(live.lvobj(), y, LV_ANIM_OFF);
    });
  });
}

//-----------------------------------------------------------------------------

PageGroup::PageGroup(EdgeTxIcon icon, const char* title, const PageDef* pages) :
    PageGroupBase(PAGE_GROUP_BODY_Y, icon)
{
  initRequiredWindowAs<PageGroupHeaderBase, PageGroupHeader>(header, this, icon,
                                                            title);

  for (int i = 0; pages[i].icon < EDGETX_ICONS_COUNT; i += 1) {
    if (pages[i].create)
      addTab(pages[i].create(pages[i]));
  }

#if defined(HARDWARE_TOUCH)
#if VERSION_MAJOR == 2
  addCustomButton(0, 0, [=]() { onCancel(); }, "nav.back", "Back");
#else
  addCustomButton(0, 0, [=]() { QuickMenu::openQuickMenu(); },
                  "nav.quick_menu", "Quick menu");
  addCustomButton(LCD_W - EdgeTxStyles::MENU_HEADER_HEIGHT, 0,
                  [=]() { onCancel(); }, "nav.back", "Back");
#endif
#endif

  setCloseHandler([]{
    storageCheck(true);
    auto viewMain = ViewMain::instance();
    if (viewMain) viewMain->updateTopbarVisibility();
  });
}

//-----------------------------------------------------------------------------

class TabsGroupHeader : public PageGroupHeaderBase
{
 public:
  TabsGroupHeader(TabsGroup* menu, EdgeTxIcon icon, const char* parentTitle) :
      PageGroupHeaderBase(menu, TabsGroup::TABS_GROUP_BODY_Y, icon, parentTitle, menu)
  {
#if PORTRAIT && VERSION_MAJOR > 2
    parentLabel.with([](lv_obj_t* obj) {
      lv_obj_set_pos(obj, PageGroup::PAGE_GROUP_TOP_BAR_H + PAD_LARGE,
                     PAD_MEDIUM * 2);
      lv_obj_set_size(obj,
                      LCD_W - PageGroup::PAGE_GROUP_TOP_BAR_H * 2 -
                          PAD_LARGE * 2,
                      PageGroup::PAGE_GROUP_TOP_BAR_H - PAD_MEDIUM * 2);
    });

    withLive([&](LiveWindow& live) {
      auto sep = lv_obj_create(live.lvobj());
      if (!requireLvObj(sep)) return false;
      etx_solid_bg(sep);
      lv_obj_set_pos(sep, 0, EdgeTxStyles::MENU_HEADER_HEIGHT);
      lv_obj_set_size(sep, LCD_W,
                      TabsGroup::TABS_GROUP_TOP_BAR_H -
                          EdgeTxStyles::MENU_HEADER_HEIGHT);
      return true;
    });

    titleLabel.with([](lv_obj_t* obj) {
      lv_obj_set_style_pad_left(obj, PAD_MEDIUM, LV_PART_MAIN);
      lv_obj_set_style_pad_top(obj, 1, LV_PART_MAIN);
      lv_obj_set_pos(obj, 0, TabsGroup::TABS_GROUP_TOP_BAR_H);
      lv_obj_set_size(obj, LCD_W, TabsGroup::TABS_GROUP_ALT_TITLE_H);
    });
#endif

#if VERSION_MAJOR > 2
    prevBtn = new (std::nothrow) IconButton(this, ICON_BTN_PREV, LCD_W - PageGroup::PAGE_GROUP_BACK_BTN_W * 3, PAD_MEDIUM, [=]() {
      prevTab();
      return 0;
    });

    nextBtn = new (std::nothrow) IconButton(this, ICON_BTN_NEXT, LCD_W - PageGroup::PAGE_GROUP_BACK_BTN_W * 2, PAD_MEDIUM, [=]() {
      nextTab();
      return 0;
    });
#endif
  }

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "TabsGroupHeader"; }
#endif

  void chgTab(int dir) override
  {
    int idx = currentIndex;
    idx += dir;
    if (idx < 0) idx = pages.size() - 1;
    else if (idx >= (int)pages.size()) idx = 0;
    menu->setCurrentTab(idx);
  }

  void hidePageButtons()
  {
    if (prevBtn) prevBtn->hide();
    if (nextBtn) nextBtn->hide();
  }

 protected:
  IconButton* nextBtn = nullptr;
  IconButton* prevBtn = nullptr;
};

//-----------------------------------------------------------------------------

TabsGroup::TabsGroup(EdgeTxIcon icon, const char* parentLabel) :
    PageGroupBase(TABS_GROUP_BODY_Y, icon)
{
  initRequiredWindowAs<PageGroupHeaderBase, TabsGroupHeader>(
      header, this, icon, parentLabel);

#if defined(HARDWARE_TOUCH)
#if VERSION_MAJOR == 2
  addCustomButton(0, 0, [=]() { onCancel(); }, "nav.back", "Back");
#else
  addCustomButton(0, 0, [=]() { QuickMenu::openQuickMenu(); },
                  "nav.quick_menu", "Quick menu");
  addCustomButton(LCD_W - EdgeTxStyles::MENU_HEADER_HEIGHT, 0,
                  [=]() { onCancel(); }, "nav.back", "Back");
#endif
#endif
}

void TabsGroup::hidePageButtons()
{
  header.with([](PageGroupHeaderBase& header) {
    static_cast<TabsGroupHeader&>(header).hidePageButtons();
  });
}
