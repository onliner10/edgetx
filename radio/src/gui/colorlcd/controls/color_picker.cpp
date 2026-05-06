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

#include "color_picker.h"

#include <new>

#include "color_list.h"
#include "dialog.h"
#include "etx_lv_theme.h"

#if LANDSCAPE
static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1),
                                     LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
#else
static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT,
                                     LV_GRID_TEMPLATE_LAST};
#endif

class ColorEditorPopup : public BaseDialog
{
  ColorSwatch* colorPad = nullptr;
  StaticText* hexStr = nullptr;
  uint32_t m_color;
  uint32_t origColor;
  std::function<void(uint32_t)> setValue;
  COLOR_EDITOR_FMT format;

  void updateColor(uint32_t c)
  {
    m_color = c;

    uint8_t r, g, b;

    if (format == ETX_RGB565) {
      auto rgb = COLOR_VAL(colorToRGB(m_color));
      r = GET_RED(rgb);
      g = GET_GREEN(rgb);
      b = GET_BLUE(rgb);
    } else {
      auto rgb = color32ToRGB(m_color);
      r = GET_RED32(rgb);
      g = GET_GREEN32(rgb);
      b = GET_BLUE32(rgb);
    }

    if (colorPad) colorPad->setColor(r, g, b);

    char s[10];
    sprintf(s, "%02X%02X%02X", r, g, b);
    if (hexStr) hexStr->setText(s);
  }

  void onCancel() override
  {
    if (setValue) setValue(origColor);
    this->deleteLater();
  }

 public:
  ColorEditorPopup(uint32_t color, std::function<void(uint32_t)> _setValue,
                   COLOR_EDITOR_FMT fmt) :
      BaseDialog(STR_COLOR_PICKER, false, COLOR_EDIT_WIDTH, COLOR_EDIT_HEIGHT),
      origColor(color),
      setValue(std::move(_setValue)),
      format(fmt)
  {
    FlexGridLayout grid(col_dsc, row_dsc);
    auto line = form.valueOr<FormLine*>(
        nullptr, [&](Window& formWindow) { return formWindow.newLine(grid); });
    if (!line) return;

    rect_t r{0, 0, CE_SZ, CE_SZ};
    auto cedit = Window::makeLive<ColorEditor>(
        line, r, color, [=](uint32_t c) { updateColor(c); }, format,
        THM_COLOR_EDITOR);
    if (!cedit) {
      failClosed();
      return;
    }
    cedit->withLive([](Window::LiveWindow& live) {
      lv_obj_set_style_grid_cell_x_align(live.lvobj(), LV_GRID_ALIGN_CENTER, 0);
    });

    auto vbox = Window::makeLive<Window>(line, rect_t{});
    if (!vbox) {
      failClosed();
      return;
    }
    vbox->withLive([](Window::LiveWindow& live) {
      lv_obj_set_style_grid_cell_x_align(live.lvobj(), LV_GRID_ALIGN_CENTER, 0);
    });
    vbox->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_MEDIUM, r.w, r.h);

    auto hbox = Window::makeLive<Window>(vbox, rect_t{});
    if (!hbox) {
      failClosed();
      return;
    }
    hbox->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_MEDIUM);
    hbox->withLive([](Window::LiveWindow& live) {
      lv_obj_set_flex_align(live.lvobj(), LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_SPACE_AROUND);
    });

    colorPad = Window::makeLive<ColorSwatch>(
        hbox, rect_t{0, 0, COLOR_PAD_WIDTH, EdgeTxStyles::UI_ELEMENT_HEIGHT},
        COLOR_THEME_PRIMARY1);

    hexStr = Window::makeLive<StaticText>(
        hbox, rect_t{0, 0, EdgeTxStyles::EDIT_FLD_WIDTH, 0}, "",
        COLOR_THEME_PRIMARY1_INDEX, FONT(L));
    if (!colorPad || !hexStr) {
      failClosed();
      return;
    }

    updateColor(color);

    hbox = Window::makeLive<Window>(vbox, rect_t{});
    if (!hbox) {
      failClosed();
      return;
    }
    hbox->padAll(PAD_TINY);
    hbox->setFlexLayout(LV_FLEX_FLOW_ROW_WRAP, PAD_MEDIUM);
    hbox->withLive([](Window::LiveWindow& live) {
      lv_obj_set_flex_align(live.lvobj(), LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_AROUND);
    });

    auto thmBtn =
        Window::makeLive<TextButton>(hbox, rect_t{0, 0, BTN_W, 0}, STR_THEME);
    auto fxdBtn =
        Window::makeLive<TextButton>(hbox, rect_t{0, 0, BTN_W, 0}, STR_FIXED);
    auto hsvBtn =
        Window::makeLive<TextButton>(hbox, rect_t{0, 0, BTN_W, 0}, "HSV");
    auto rgbBtn =
        Window::makeLive<TextButton>(hbox, rect_t{0, 0, BTN_W, 0}, "RGB");
    if (!thmBtn || !fxdBtn || !hsvBtn || !rgbBtn) {
      failClosed();
      return;
    }

    rgbBtn->setPressHandler([=]() {
      cedit->setColorEditorType(RGB_COLOR_EDITOR);
      hsvBtn->check(false);
      thmBtn->check(false);
      fxdBtn->check(false);
      return 1;
    });

    hsvBtn->setPressHandler([=]() {
      cedit->setColorEditorType(HSV_COLOR_EDITOR);
      rgbBtn->check(false);
      thmBtn->check(false);
      fxdBtn->check(false);
      return 1;
    });

    thmBtn->setPressHandler([=]() {
      cedit->setColorEditorType(THM_COLOR_EDITOR);
      rgbBtn->check(false);
      hsvBtn->check(false);
      fxdBtn->check(false);
      return 1;
    });

    fxdBtn->setPressHandler([=]() {
      cedit->setColorEditorType(FXD_COLOR_EDITOR);
      rgbBtn->check(false);
      hsvBtn->check(false);
      thmBtn->check(false);
      return 1;
    });

    // color editor defaults to HSV
    thmBtn->check(true);
    thmBtn->focus();

    hbox = Window::makeLive<Window>(
        vbox, rect_t{0, height() - EdgeTxStyles::UI_ELEMENT_HEIGHT - PAD_SMALL,
                     0, 0});
    if (!hbox) {
      failClosed();
      return;
    }
    hbox->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_MEDIUM);
    hbox->withLive([](Window::LiveWindow& live) {
      lv_obj_set_flex_align(live.lvobj(), LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_SPACE_BETWEEN);
      lv_obj_set_flex_grow(live.lvobj(), 1);
    });

    if (!Window::makeLive<TextButton>(hbox, rect_t{0, 0, BTN_W, 0}, STR_CANCEL,
                                      [=]() -> int8_t {
                                        onCancel();
                                        return 0;
                                      })) {
      failClosed();
      return;
    }

    if (!Window::makeLive<TextButton>(hbox, rect_t{0, 0, BTN_W, 0}, STR_SAVE,
                                      [=]() -> int8_t {
                                        if (setValue) setValue(m_color);
                                        this->deleteLater();
                                        return 0;
                                      })) {
      failClosed();
      return;
    }
  }

  static LAYOUT_VAL_SCALED(CE_SZ, 182)
#if NARROW_LAYOUT
      static LAYOUT_ORIENTATION(COLOR_EDIT_WIDTH, LCD_W * 0.95, LCD_W * 0.7)
#else
      static LAYOUT_ORIENTATION(COLOR_EDIT_WIDTH, LCD_W * 0.8, LCD_W * 0.7)
#endif
          static LAYOUT_ORIENTATION(COLOR_EDIT_HEIGHT, LCD_H * 0.9, LV_SIZE_CONTENT) static LAYOUT_VAL_SCALED(
              COLOR_PAD_WIDTH,
              52) static LAYOUT_VAL_SCALED(BTN_W,
                                           80) static LAYOUT_VAL_SCALED(BTN_PAD_TOP,
                                                                        60)
};

ColorPicker::ColorPicker(Window* parent, const rect_t& rect,
                         std::function<uint32_t()> _getValue,
                         std::function<void(uint32_t)> _setValue,
                         COLOR_EDITOR_FMT fmt) :
    Button(parent, {rect.x, rect.y,
                    rect.w == 0 ? ColorEditorPopup::COLOR_PAD_WIDTH : rect.w,
                    EdgeTxStyles::UI_ELEMENT_HEIGHT}),
    getValue(std::move(_getValue)),
    setValue(std::move(_setValue)),
    format(fmt)
{
  updateColor(getValue());
}

void ColorPicker::onLiveClicked(Window::LiveWindow&)
{
  updateColor(getValue());
  new (std::nothrow) ColorEditorPopup(
      color,
      [=](uint32_t c) {
        setValue(c);
        updateColor(c);
      },
      format);
}

void ColorPicker::updateColor(uint32_t c)
{
  color = c;

  uint8_t r, g, b;

  if (format == ETX_RGB565) {
    auto rgb = COLOR_VAL(colorToRGB(color));
    r = GET_RED(rgb);
    g = GET_GREEN(rgb);
    b = GET_BLUE(rgb);
  } else {
    auto rgb = color32ToRGB(color);
    r = GET_RED32(rgb);
    g = GET_GREEN32(rgb);
    b = GET_BLUE32(rgb);
  }

  auto lvcolor = lv_color_make(r, g, b);
  withLive([&](Window::LiveWindow& live) {
    lv_obj_set_style_bg_color(live.lvobj(), lvcolor, LV_PART_MAIN);
  });
}
