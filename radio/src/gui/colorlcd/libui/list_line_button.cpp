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

#include "list_line_button.h"

#include <algorithm>

#include "edgetx.h"
#include "etx_lv_theme.h"
#include "mainwindow.h"

#if defined(SIMU)
static bool forceFmBufferMallocFailure = false;
static bool forceListLineLabelCreateFailure = false;

void listLineButtonForceFmBufferMallocFailureForTest(bool force)
{
  forceFmBufferMallocFailure = force;
}

static void listLineButtonForceLabelCreateFailureForTest(bool force)
{
  forceListLineLabelCreateFailure = force;
}
#endif

static lv_obj_t* list_line_label_create(lv_obj_t* parent)
{
#if defined(SIMU)
  if (forceListLineLabelCreateFailure) return nullptr;
#endif
  return etx_label_create(parent);
}

static void input_mix_line_constructor(const lv_obj_class_t* class_p,
                                       lv_obj_t* obj)
{
  etx_std_style(obj, LV_PART_MAIN, PAD_TINY);
}

static const lv_obj_class_t input_mix_line_class = {
    .base_class = &lv_btn_class,
    .constructor_cb = input_mix_line_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = ListLineButton::GRP_W,
    .height_def = ListLineButton::BTN_H,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_TRUE,
    .instance_size = sizeof(lv_btn_t),
};

static lv_obj_t* input_mix_line_create(lv_obj_t* parent)
{
  return etx_create(&input_mix_line_class, parent);
}

ListLineButton::ListLineButton(Window* parent, uint8_t index) :
    ButtonBase(parent, rect_t{}, nullptr, input_mix_line_create), index(index)
{
}

void ListLineButton::onLiveCheckEvents(Window::LiveWindow& live)
{
  check(isActive());
  ButtonBase::onLiveCheckEvents(live);
}

InputMixButtonBase::InputMixButtonBase(Window* parent, uint8_t index) :
    ListLineButton(parent, index)
{
  setWidth(BTN_W);
  setHeight(ListLineButton::BTN_H);
  padAll(PAD_ZERO);
}

InputMixButtonBase::~InputMixButtonBase()
{
  if (fm_buffer) free(fm_buffer);
}

static void setLineLabelText(RequiredLvObj& label, const char* text,
                             coord_t width)
{
  label.with([&](lv_obj_t* obj) {
    if (getTextWidth(text, 0, FONT(STD)) > width)
      lv_obj_add_state(obj, LV_STATE_USER_1);
    else
      lv_obj_clear_state(obj, LV_STATE_USER_1);
    lv_label_set_text(obj, text);
  });
}

bool InputMixButtonBase::ensureLineLabel(RequiredLvObj& label, coord_t x,
                                         coord_t y, coord_t w, coord_t h)
{
  if (label.isPresent()) return true;

  return initRequiredLvObj(label, list_line_label_create, [&](lv_obj_t* obj) {
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    etx_font(obj, FONT_XS_INDEX, LV_STATE_USER_1);
  });
}

void InputMixButtonBase::setWeight(gvar_t value, gvar_t min, gvar_t max)
{
  if (!ensureLineLabel(weight, WGT_X, WGT_Y, WGT_W, WGT_H)) return;

  char s[32];
  getValueOrSrcVarString(s, sizeof(s), value, 0, "%");
  setLineLabelText(weight, s, WGT_W);
}

void InputMixButtonBase::setSource(mixsrc_t idx)
{
  if (!ensureLineLabel(source, SRC_X, SRC_Y, SRC_W, SRC_H)) return;

  char* s = getSourceString(idx);
  setLineLabelText(source, s, SRC_W);
}

void InputMixButtonBase::setOpts(const char* s)
{
  if (!ensureLineLabel(opts, OPT_X, OPT_Y, OPT_W, OPT_H)) return;

  setLineLabelText(opts, s, OPT_W);
}

void InputMixButtonBase::setFlightModes(uint16_t modes)
{
  bool handled = withLive([&](LiveWindow& live) {
    auto obj = live.lvobj();

    if (!modelFMEnabled()) return;
    if (modes == fm_modes) return;
    fm_modes = modes;

    if (!fm_modes) {
      if (!fm_canvas) return;
      lv_obj_del(fm_canvas);
      free(fm_buffer);
      fm_canvas = nullptr;
      fm_buffer = nullptr;
      updateHeight();
      return;
    }

    if (!fm_canvas) {
      auto newCanvas = lv_canvas_create(obj);
      if (!newCanvas) {
        fm_modes = 0;
        updateHeight();
        return;
      }

#if defined(SIMU)
      auto newBuffer = forceFmBufferMallocFailure
                           ? nullptr
                           : malloc(FM_CANVAS_WIDTH * FM_CANVAS_HEIGHT);
#else
      auto newBuffer = malloc(FM_CANVAS_WIDTH * FM_CANVAS_HEIGHT);
#endif
      if (!newBuffer) {
        lv_obj_del(newCanvas);
        fm_modes = 0;
        updateHeight();
        return;
      }

      fm_canvas = newCanvas;
      fm_buffer = newBuffer;
      lv_canvas_set_buffer(fm_canvas, fm_buffer, FM_CANVAS_WIDTH,
                           FM_CANVAS_HEIGHT, LV_IMG_CF_ALPHA_8BIT);
      lv_obj_set_pos(fm_canvas, FM_X, FM_Y);

      lv_obj_set_style_img_recolor(fm_canvas, makeLvColor(COLOR_THEME_SECONDARY1), LV_PART_MAIN);
      lv_obj_set_style_img_recolor_opa(fm_canvas, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_img_recolor(fm_canvas, makeLvColor(COLOR_THEME_PRIMARY1), LV_PART_MAIN | LV_STATE_CHECKED);
      lv_obj_set_style_img_recolor_opa(fm_canvas, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
    }

    lv_canvas_fill_bg(fm_canvas, lv_color_black(), LV_OPA_TRANSP);

    const MaskBitmap* mask = getBuiltinIcon(ICON_TEXTLINE_FM);
    lv_coord_t w = mask->width;
    lv_coord_t h = mask->height;

    coord_t x = 0;
    lv_canvas_copy_buf(fm_canvas, mask->data, x, 0, w, h);
    x += (w + PAD_TINY);

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_opa = LV_OPA_COVER;

    const lv_font_t* font = getFont(FONT(XS));
    label_dsc.font = font;

    for (int i = 0; i < MAX_FLIGHT_MODES; i++) {
      char s[] = " ";
      s[0] = '0' + i;
      if (fm_modes & (1 << i)) {
        label_dsc.color = lv_color_make(0x7f, 0x7f, 0x7f);
      } else {
        lv_canvas_draw_rect(fm_canvas, x, 0, FM_W, PAD_THREE, &rect_dsc);
        label_dsc.color = lv_color_white();
      }
      lv_canvas_draw_text(fm_canvas, x, 0, FM_W, &label_dsc, s);
      x += FM_W;
    }

    updateHeight();
  });
  if (!handled) {
    fm_modes = 0;
    return;
  }
}

#if defined(SIMU)
bool listLineButtonMissingFmBufferLeavesNoCanvasForTest()
{
  class TestInputMixButton : public InputMixButtonBase
  {
   public:
    TestInputMixButton(Window* parent) : InputMixButtonBase(parent, 0) {}

    void refresh() override {}
    void updatePos(coord_t, coord_t) override {}
    void swapLvglGroup(InputMixButtonBase*) override {}

    bool hasFlightModeCanvas() const { return fm_canvas != nullptr; }
    bool hasFlightModeBuffer() const { return fm_buffer != nullptr; }

   protected:
    bool isActive() const override { return false; }
  };

  g_eeGeneral.modelFMDisabled = 0;
  g_model.modelFMDisabled = OVERRIDE_ON;

  auto button = new TestInputMixButton(MainWindow::instance());
  listLineButtonForceFmBufferMallocFailureForTest(true);
  button->setFlightModes(1);
  listLineButtonForceFmBufferMallocFailureForTest(false);

  return !button->hasFlightModeCanvas() && !button->hasFlightModeBuffer();
}

bool listLineButtonLabelAllocationFailureFailsClosedForTest()
{
  class TestInputMixButton : public InputMixButtonBase
  {
   public:
    TestInputMixButton(Window* parent) : InputMixButtonBase(parent, 0) {}

    void refresh() override {}
    void updatePos(coord_t, coord_t) override {}
    void swapLvglGroup(InputMixButtonBase*) override {}

   protected:
    bool isActive() const override { return false; }
  };

  auto button = new (std::nothrow) TestInputMixButton(MainWindow::instance());
  if (!button || !button->isAvailable()) {
    delete button;
    return false;
  }

  listLineButtonForceLabelCreateFailureForTest(true);
  button->setWeight(100, -100, 100);
  listLineButtonForceLabelCreateFailureForTest(false);

  button->setSource(MIXSRC_FIRST_INPUT);
  button->setOpts("Opt");
  button->setFlightModes(1);
  button->checkEvents();

  bool ok = !button->isAvailable() && !button->isVisible() &&
            !button->automationClickable();
  delete button;
  return ok;
}

bool listLineGroupLabelAllocationFailureFailsClosedForTest()
{
  class TestInputMixGroup : public InputMixGroupBase
  {
   public:
    explicit TestInputMixGroup(Window* parent) :
        InputMixGroupBase(parent, MIXSRC_FIRST_INPUT)
    {
    }
  };

  listLineButtonForceLabelCreateFailureForTest(true);
  auto group = new (std::nothrow) TestInputMixGroup(MainWindow::instance());
  listLineButtonForceLabelCreateFailureForTest(false);

  if (!group) return false;
  group->refresh();
  group->adjustHeight();

  bool ok = !group->isAvailable() && !group->isVisible();
  delete group;
  return ok;
}
#endif

void InputMixButtonBase::onLiveCheckEvents(Window::LiveWindow& live)
{
  ListLineButton::onLiveCheckEvents(live);
  if (fm_canvas) {
    bool chkd = lv_obj_get_state(fm_canvas) & LV_STATE_CHECKED;
    if (chkd != this->checked()) {
      if (chkd)
        lv_obj_clear_state(fm_canvas, LV_STATE_CHECKED);
      else
        lv_obj_add_state(fm_canvas, LV_STATE_CHECKED);
    }
  }
}

void InputMixButtonBase::updateHeight()
{
#if NARROW_LAYOUT
  coord_t h = ListLineButton::BTN_H;
  if (fm_canvas)
    h += FM_CANVAS_HEIGHT + PAD_TINY;
  setHeight(h);
#endif
}

static void group_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
  etx_std_style(obj, LV_PART_MAIN, PAD_TINY);
}

static const lv_obj_class_t group_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = group_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = ListLineButton::GRP_W,
    .height_def = LV_SIZE_CONTENT,
    .editable = LV_OBJ_CLASS_EDITABLE_FALSE,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_FALSE,
    .instance_size = sizeof(lv_obj_t),
};

static lv_obj_t* group_create(lv_obj_t* parent)
{
  return etx_create(&group_class, parent);
}

InputMixGroupBase::InputMixGroupBase(Window* parent, mixsrc_t idx) :
    Window(parent, rect_t{}, group_create), idx(idx)
{
  setWindowFlag(NO_FOCUS | NO_CLICK);
  initRequiredLvObj(label, list_line_label_create, [&](lv_obj_t* obj) {
    etx_font(obj, FONT_XS_INDEX, LV_STATE_USER_1);
  });
}

void InputMixGroupBase::_adjustHeight(coord_t y)
{
  if (getLineCount() == 0) setHeight(ListLineButton::BTN_H + PAD_SMALL * 2);

  for (auto it = lines.cbegin(); it != lines.cend(); ++it) {
    auto line = *it;
    line->updateHeight();
    line->updatePos(InputMixButtonBase::LN_X, y);
    y += line->height() + PAD_OUTLINE;
  }
  setHeight(y + PAD_BORDER * 2 + PAD_OUTLINE);
}

void InputMixGroupBase::adjustHeight()
{
  _adjustHeight(0);
}

void InputMixGroupBase::addLine(InputMixButtonBase* line)
{
  if (!line || !line->withLive([](Window::LiveWindow&) { return true; }))
    return;

  auto l = std::find_if(lines.begin(), lines.end(),
                        [=](const InputMixButtonBase* l) -> bool {
                          return line->getIndex() <= l->getIndex();
                        });

  if (l != lines.end())
    lines.insert(l, line);
  else
    lines.emplace_back(line);

  adjustHeight();
}

bool InputMixGroupBase::removeLine(InputMixButtonBase* line)
{
  auto l = std::find_if(
      lines.begin(), lines.end(),
      [=](const InputMixButtonBase* l) -> bool { return l == line; });

  if (l != lines.end()) {
    lines.erase(l);
    adjustHeight();
    return true;
  }

  return false;
}

void InputMixGroupBase::refresh()
{
  char* s = getSourceString(idx);
  setLineLabelText(label, s, InputMixButtonBase::LN_X - PAD_TINY);
}

int InputMixGroupBase::getLineNumber(uint8_t index)
{
  int n = 0;
  auto l = std::find_if(lines.begin(), lines.end(), [&](InputMixButtonBase* l) {
    n += 1;
    return l->getIndex() == index;
  });

  if (l != lines.end()) return n;

  return -1;
}

InputMixGroupBase* InputMixPageBase::getGroupBySrc(mixsrc_t src)
{
  auto g = std::find_if(
      groups.begin(), groups.end(),
      [=](InputMixGroupBase* g) -> bool { return g->getMixSrc() == src; });

  if (g != groups.end()) return *g;

  return nullptr;
}

void InputMixPageBase::removeGroup(InputMixGroupBase* g)
{
  auto group = std::find_if(groups.begin(), groups.end(),
                            [=](InputMixGroupBase* lh) -> bool { return lh == g; });
  if (group != groups.end()) groups.erase(group);
}

InputMixButtonBase* InputMixPageBase::getLineByIndex(uint8_t index)
{
  auto l = std::find_if(lines.begin(), lines.end(), [=](InputMixButtonBase* l) {
    return l->getIndex() == index;
  });

  if (l != lines.end()) return *l;

  return nullptr;
}

void InputMixPageBase::removeLine(InputMixButtonBase* l)
{
  auto line = std::find_if(lines.begin(), lines.end(),
                           [=](InputMixButtonBase* lh) -> bool { return lh == l; });
  if (line == lines.end()) return;

  line = lines.erase(line);
  while (line != lines.end()) {
    (*line)->setIndex((*line)->getIndex() - 1);
    ++line;
  }
}

void InputMixPageBase::addLineButton(mixsrc_t src, uint8_t index)
{
  InputMixGroupBase* group_w = getGroupBySrc(src);
  if (!group_w) {
    group_w = createGroup(form, src);
    // insertion sort
    groups.emplace_back(group_w);
    auto g = groups.rbegin();
    if (g != groups.rend()) {
      auto g_prev = g;
      ++g_prev;
      while (g_prev != groups.rend()) {
        if ((*g_prev)->getMixSrc() < (*g)->getMixSrc()) break;
        lv_obj_swap((*g)->getLvObj(), (*g_prev)->getLvObj());
        std::swap(*g, *g_prev);
        ++g;
        ++g_prev;
      }
    }
  }

  // create new line button
  auto btn = createLineButton(group_w, index);
  lv_group_focus_obj(btn->getLvObj());

  // insertion sort for the focus group
  auto l = lines.rbegin();
  if (l != lines.rend()) {
    auto l_prev = l;
    ++l_prev;
    while (l_prev != lines.rend()) {
      if ((*l_prev)->getIndex() < (*l)->getIndex()) break;
      (*l)->swapLvglGroup(*l_prev);
      std::swap(*l, *l_prev);
      // Inc index of elements after
      (*l)->setIndex((*l)->getIndex() + 1);
      ++l;
      ++l_prev;
    }
  }
}
