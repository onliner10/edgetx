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

#include <algorithm>
#include <new>

#include "edgetx.h"
#include "etx_lv_theme.h"
#include "menu.h"
#include "view_main.h"
#include "widget_settings.h"

#if defined(HARDWARE_TOUCH)
#include "touch.h"
#endif

//-----------------------------------------------------------------------------

inline WidgetOptionValueEnum widgetValueEnumFromType(WidgetOption::Type type)
{
  switch (type) {
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
    while ((int)options.size() <= idx) options.push_back(wov);
    markChanged();
  }
}

bool WidgetPersistentData::hasOption(int idx)
{
  return idx < (int)options.size();
}

void WidgetPersistentData::setDefault(int idx, const WidgetOption* opt,
                                      bool forced)
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
    addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_VER);
  }

  setPressHandler([&]() -> uint8_t {
    // When ViewMain is in "widget select mode",
    // the widget is added to a focus group
    if (!fullscreen && withLive([](LiveWindow& live) {
          return lv_obj_get_group(live.lvobj()) != nullptr;
        }))
      openMenu();
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
      FONT_XXL_INDEX,  FONT_LXL_INDEX, FONT_XL_INDEX, FONT_L_INDEX,
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

NativeWidget::NativeWidget(const WidgetFactory* factory, Window* parent,
                           const rect_t& rect, WidgetLocation location) :
    Widget(factory, parent, rect, location)
{
  delayWidgetLoad();
}

lv_color_t NativeWidget::cardColor() { return lv_color_make(248, 251, 255); }

lv_color_t NativeWidget::borderColor() { return lv_color_make(182, 202, 219); }

lv_color_t NativeWidget::pillColor() { return lv_color_make(226, 238, 247); }

lv_color_t NativeWidget::trackColor() { return lv_color_make(215, 228, 238); }

lv_color_t NativeWidget::primaryTextColor()
{
  return lv_color_make(24, 57, 84);
}

lv_color_t NativeWidget::mutedTextColor() { return lv_color_make(68, 92, 112); }

bool NativeWidget::usesCardChrome() const
{
  return isMainViewWidget() && !isCompactTopBarWidget();
}

rect_t NativeWidget::cardRect() const
{
  if (!usesCardChrome()) return {0, 0, width(), height()};

  constexpr coord_t inset = PAD_TINY;
  coord_t w = width() > 2 * inset ? width() - 2 * inset : width();
  coord_t h = height() > 2 * inset ? height() - 2 * inset : height();
  return {inset, inset, w, h};
}

rect_t NativeWidget::contentRect() const
{
  rect_t r = cardRect();
  coord_t inset = usesCardChrome() ? PAD_MEDIUM : PAD_TINY;
  if (r.h <= 54) inset = usesCardChrome() ? PAD_SMALL : PAD_TINY;

  coord_t w = r.w > 2 * inset ? r.w - 2 * inset : r.w;
  coord_t h = r.h > 2 * inset ? r.h - 2 * inset : r.h;
  return {r.x + inset, r.y + inset, w, h};
}

void NativeWidget::setObjRect(lv_obj_t* obj, coord_t x, coord_t y, coord_t w,
                              coord_t h)
{
  if (!obj) return;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
}

void NativeWidget::setObjVisible(lv_obj_t* obj, bool visible)
{
  if (!obj) return;
  if (visible)
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* NativeWidget::createFlexBox(lv_obj_t* parent, lv_flex_flow_t flow)
{
  auto obj = lv_obj_create(parent);
  if (!obj) return nullptr;
  lv_obj_remove_style_all(obj);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(obj, flow);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(obj, PAD_TINY, LV_PART_MAIN);
  lv_obj_set_style_pad_column(obj, PAD_TINY, LV_PART_MAIN);
  return obj;
}

void NativeWidget::layoutFlexBox(lv_obj_t* obj, const rect_t& rect,
                                 lv_flex_flow_t flow, coord_t gap,
                                 lv_flex_align_t main, lv_flex_align_t cross,
                                 lv_flex_align_t track)
{
  if (!obj) return;
  setObjRect(obj, rect.x, rect.y, rect.w, rect.h);
  lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(obj, flow);
  lv_obj_set_flex_align(obj, main, cross, track);
  lv_obj_set_style_pad_row(obj, gap, LV_PART_MAIN);
  lv_obj_set_style_pad_column(obj, gap, LV_PART_MAIN);
}

void NativeWidget::setFlexChild(lv_obj_t* obj, coord_t width, coord_t height,
                                uint8_t grow)
{
  if (!obj) return;
  if (width < 1) width = 1;
  if (height < 1) height = 1;
  lv_obj_set_size(obj, width, height);
  lv_obj_set_flex_grow(obj, grow);
}

FontIndex NativeWidget::cardTitleFont(const rect_t& content)
{
  return content.h <= 34 ? FONT_XXS_INDEX : FONT_XS_INDEX;
}

FontIndex NativeWidget::cardValueFont(const rect_t& content)
{
  return content.h <= 34 ? FONT_XS_INDEX : FONT_STD_INDEX;
}

FontIndex NativeWidget::cardStackTitleFont(const rect_t& content)
{
  static constexpr FontIndex titleFonts[] = {FONT_L_INDEX, FONT_STD_INDEX,
                                             FONT_XS_INDEX, FONT_XXS_INDEX};
  coord_t maxTitleH = content.h / 4;
  coord_t minTitleH = getFontHeight(LcdFlags(FONT_XXS_INDEX) << 8u);
  if (maxTitleH < minTitleH) maxTitleH = minTitleH;

  for (FontIndex font : titleFonts) {
    if (getFontHeight(LcdFlags(font) << 8u) <= maxTitleH) return font;
  }
  return FONT_XXS_INDEX;
}

FontIndex NativeWidget::cardStackValueFont(const rect_t& content)
{
  if (content.h >= 118) return FONT_XL_INDEX;
  if (content.h >= 76) return FONT_L_INDEX;
  return content.h <= 34 ? FONT_STD_INDEX : FONT_BOLD_INDEX;
}

coord_t NativeWidget::cardHeaderHeight(const rect_t& content)
{
  coord_t titleH = getFontHeight(LcdFlags(cardTitleFont(content)) << 8u);
  coord_t valueH = getFontHeight(LcdFlags(cardValueFont(content)) << 8u);
  return titleH > valueH ? titleH : valueH;
}

coord_t NativeWidget::cardGap(const rect_t& content)
{
  return content.h <= 34 ? 2 : PAD_TINY;
}

coord_t NativeWidget::cardBarHeight(const rect_t& content)
{
  if (content.h <= 34) return 8;
  if (content.h <= 54) return 10;
  return 14;
}

void NativeWidget::layoutCardHeader(lv_obj_t* row, lv_obj_t* title,
                                    lv_obj_t* value, const rect_t& rect,
                                    coord_t valueWidth)
{
  if (!row) return;

  coord_t gap = cardGap(rect);
  coord_t headerH = cardHeaderHeight(rect);
  if (valueWidth <= 0) {
    valueWidth = rect.w / 3;
    if (valueWidth < 48) valueWidth = 48;
  }
  if (valueWidth > rect.w / 2) valueWidth = rect.w / 2;

  layoutFlexBox(row, rect, LV_FLEX_FLOW_ROW, gap, LV_FLEX_ALIGN_START,
                LV_FLEX_ALIGN_CENTER);

  if (title) {
    etx_font(title, cardTitleFont(rect));
    lv_obj_set_style_text_color(title, mutedTextColor(), LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    setFlexChild(
        title,
        rect.w > valueWidth + gap ? rect.w - valueWidth - gap : rect.w / 2,
        headerH, 1);
  }

  if (value) {
    etx_font(value, cardValueFont(rect));
    lv_obj_set_style_text_color(value, primaryTextColor(), LV_PART_MAIN);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    setFlexChild(value, valueWidth, headerH);
  }
}

void NativeWidget::layoutCardStack(lv_obj_t* column, lv_obj_t* title,
                                   lv_obj_t* value, const rect_t& rect)
{
  if (!column) return;

  coord_t gap = cardGap(rect);
  FontIndex titleFont = cardStackTitleFont(rect);
  coord_t titleH = getFontHeight(LcdFlags(titleFont) << 8u);
  coord_t usableH = rect.h > gap ? rect.h - gap : rect.h;
  if (usableH <= titleH)
    titleH = usableH > 2 ? usableH / 2 : usableH;
  else if (titleH > usableH / 2)
    titleH = usableH / 2;
  coord_t valueH = usableH > titleH ? usableH - titleH : 1;

  static constexpr FontIndex valueFonts[] = {
      FONT_XXL_INDEX, FONT_LXL_INDEX, FONT_XL_INDEX,   FONT_L_INDEX,
      FONT_BOLD_INDEX, FONT_STD_INDEX, FONT_XS_INDEX, FONT_XXS_INDEX};
  const char* valueText = value ? lv_label_get_text(value) : "";
  if (!valueText || valueText[0] == '\0') valueText = "0000";
  FontIndex valueFont = fitTextFont(valueText, rect.w, valueH, valueFonts,
                                    DIM(valueFonts));
  coord_t valueFontH = getFontHeight(LcdFlags(valueFont) << 8u);
  coord_t valueY = titleH + gap;
  if (valueH > valueFontH) valueY += (valueH - valueFontH) / 2;

  setObjRect(column, rect.x, rect.y, rect.w, rect.h);
  lv_obj_set_layout(column, 0);

  if (title) {
    etx_font(title, titleFont);
    lv_obj_set_style_text_color(title, mutedTextColor(), LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    setObjRect(title, 0, 0, rect.w, titleH);
  }

  if (value) {
    etx_font(value, valueFont);
    lv_obj_set_style_text_color(value, primaryTextColor(), LV_PART_MAIN);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    setObjRect(value, 0, valueY, rect.w, valueFontH);
  }
}

rect_t NativeWidget::insetRect(const rect_t& rect, coord_t inset)
{
  if (rect.w <= 2 * inset || rect.h <= 2 * inset) return rect;
  return {static_cast<coord_t>(rect.x + inset),
          static_cast<coord_t>(rect.y + inset),
          static_cast<coord_t>(rect.w - 2 * inset),
          static_cast<coord_t>(rect.h - 2 * inset)};
}

FontIndex NativeWidget::fitTextFont(const char* text, coord_t width,
                                    coord_t height, const FontIndex* fonts,
                                    uint8_t fontCount)
{
  for (uint8_t i = 0; i < fontCount; i += 1) {
    FontIndex font = fonts[i];
    LcdFlags flags = LcdFlags(font) << 8u;
    if (getFontHeight(flags) <= height && getTextWidth(text, 0, flags) <= width)
      return font;
  }
  return fontCount > 0 ? fonts[fontCount - 1] : FONT_XXS_INDEX;
}

void NativeWidget::layoutText(lv_obj_t* obj, const rect_t& rect, FontIndex font,
                              lv_color_t color, lv_text_align_t align)
{
  if (!obj) return;
  coord_t fh = getFontHeight(LcdFlags(font) << 8u);
  coord_t y = rect.y + (rect.h > fh ? (rect.h - fh) / 2 : 0);
  etx_font(obj, font);
  lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
  lv_obj_set_style_text_align(obj, align, LV_PART_MAIN);
  setObjRect(obj, rect.x, y, rect.w, fh);
}

void NativeWidget::layoutInlineMetric(lv_obj_t* label, lv_obj_t* value,
                                      const rect_t& rect, coord_t valueWidth,
                                      FontIndex font)
{
  if (valueWidth > rect.w) valueWidth = rect.w / 2;
  coord_t labelW = rect.w > valueWidth ? rect.w - valueWidth : rect.w / 2;
  layoutText(label, {rect.x, rect.y, labelW, rect.h}, font, mutedTextColor(),
             LV_TEXT_ALIGN_LEFT);
  layoutText(
      value,
      {static_cast<coord_t>(rect.x + labelW), rect.y, valueWidth, rect.h}, font,
      primaryTextColor(), LV_TEXT_ALIGN_RIGHT);
}

void NativeWidget::layoutPillBar(lv_obj_t* track, lv_obj_t* fill,
                                 const rect_t& rect, uint8_t percent,
                                 lv_color_t fillColor)
{
  if (percent > 100) percent = 100;
  setObjRect(track, rect.x, rect.y, rect.w, rect.h);
  if (track) {
    lv_obj_set_style_bg_color(track, trackColor(), LV_PART_MAIN);
    lv_obj_set_style_radius(track, PILL_RADIUS, LV_PART_MAIN);
  }
  coord_t fillW = (coord_t)((int)rect.w * percent / 100);
  setObjRect(fill, rect.x, rect.y, fillW, rect.h);
  if (fill) {
    lv_obj_set_style_bg_color(fill, fillColor, LV_PART_MAIN);
    lv_obj_set_style_radius(fill, PILL_RADIUS, LV_PART_MAIN);
  }
}

void NativeWidget::delayedInit()
{
  initRequiredLvObj(
      card,
      [](lv_obj_t* parent) {
        auto obj = lv_obj_create(parent);
        if (obj) {
          lv_obj_remove_style_all(obj);
          lv_obj_clear_flag(obj,
                            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
          lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
          lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
          lv_obj_set_style_radius(obj, CARD_RADIUS, LV_PART_MAIN);
        }
        return obj;
      },
      [](lv_obj_t*) {});

  card.with([&](lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, cardColor(), LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, borderColor(), LV_PART_MAIN);
    lv_obj_move_background(obj);
  });

  withLive([&](LiveWindow& live) {
    createContent(live.lvobj());
    return true;
  });

  update();
  foreground();
}

void NativeWidget::onUpdate()
{
  card.with([&](lv_obj_t* obj) {
    setObjVisible(obj, usesCardChrome());
    rect_t r = cardRect();
    setObjRect(obj, r.x, r.y, r.w, r.h);
    lv_obj_move_background(obj);
  });

  layoutContent(contentRect());
  invalidateNativeRefresh();
}

void NativeWidget::onForeground()
{
  WidgetRefreshKey key;
  key.add(getPersistentDataRevision())
      .add((int32_t)width())
      .add((int32_t)height())
      .add(isFullscreen())
      .add(contentRefreshKey());

  uint32_t currentKey = key.value();
  if (!refreshPending && currentKey == lastRefreshKey) return;

  lastRefreshKey = currentKey;
  refreshPending = false;
  refreshContent();
}

void Widget::delayWidgetLoad() { delayLoad(); }

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

void Widget::onUpdateWithoutRefresh() { onUpdate(); }

void Widget::openMenu()
{
  auto viewMain = ViewMain::instance();
  if (isMainViewWidget() && viewMain && viewMain->isAppMode()) {
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

    removeFromGroup();

    // re-enable scroll chaining (sliding main view)
    addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_VER);
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

    if (!withLive([](LiveWindow& live) {
          return lv_obj_get_group(live.lvobj()) != nullptr;
        }))
      addToGroup(lv_group_get_default());

    // disable scroll chaining (sliding main view)
    clearFlag(LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    clearFlag(LV_OBJ_FLAG_SCROLL_CHAIN_VER);
  }

  // set group in editing mode (keys LEFT / RIGHT)
  if (enableFullScreenRE())
    lv_group_set_editing(lv_group_get_default(), enable);

  onFullscreen(enable);

  if (fullscreen) updateWithoutRefresh();
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
    if (!focusBorder.isPresent()) {
      lv_style_init(&borderStyle);
      lv_style_set_line_width(&borderStyle, PAD_BORDER);
      lv_style_set_line_opa(&borderStyle, LV_OPA_COVER);
      lv_style_set_line_color(&borderStyle, makeLvColor(COLOR_THEME_ACTIVE));

      borderPts[0] = {1, 1};
      borderPts[1] = {(lv_coord_t)(width() - 1), 1};
      borderPts[2] = {(lv_coord_t)(width() - 1), (lv_coord_t)(height() - 1)};
      borderPts[3] = {1, (lv_coord_t)(height() - 1)};
      borderPts[4] = {1, 1};

      if (!withLive([&](LiveWindow& live) {
            auto obj = lv_line_create(live.lvobj());
            if (!requireLvObj(obj)) return false;
            focusBorder.reset(obj);
            lv_obj_add_style(obj, &borderStyle, LV_PART_MAIN);
            lv_line_set_points(obj, borderPts, 5);
            return true;
          }))
        return;

      if (!hasFocus()) {
        focusBorder.with(
            [](lv_obj_t* obj) { lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN); });
      }

      setFocusHandler([=](bool hasFocus) {
        focusBorder.with([&](lv_obj_t* obj) {
          if (hasFocus)
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
          else
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        });
        auto viewMain = ViewMain::instance();
        if (viewMain) viewMain->refreshWidgetSelectTimer();
      });

      addToGroup(lv_group_get_default());
    }
  } else {
    if (focusBorder.isPresent()) {
      focusBorder.with([](lv_obj_t* obj) { lv_obj_del(obj); });
      focusBorder.reset(nullptr);
      setFocusHandler(nullptr);
      removeFromGroup();
    }
  }
}

WidgetPersistentData* Widget::getPersistentData()
{
  return location.persistentData();
}

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

WidgetFactory::~WidgetFactory() { unregisterWidget(*this); }

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
  widgets.erase(std::remove_if(widgets.begin(), widgets.end(),
                               [&](const auto& registered) {
                                 return &registered.get() == &factory;
                               }),
                widgets.end());
}

const WidgetFactory* WidgetFactory::getWidgetFactory(const char* name)
{
  if (!name) return nullptr;

  if (!strcmp(name, "ModelBmp")) {
    name = "ModelName";
  }

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
