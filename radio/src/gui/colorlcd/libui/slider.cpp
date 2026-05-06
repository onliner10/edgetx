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

#include "slider.h"

#include "form.h"
#include "etx_lv_theme.h"
#include "mainwindow.h"

#include <new>

#if defined(SIMU)
static bool forceSliderFormFieldCreateFailure = false;

void sliderForceFormFieldCreateFailureForTest(bool force)
{
  forceSliderFormFieldCreateFailure = force;
}
#endif

static FormField* createSliderFormField(Window* parent, LvglCreate create)
{
#if defined(SIMU)
  if (forceSliderFormFieldCreateFailure) return nullptr;
#endif
  return new (std::nothrow) FormField(parent, rect_t{}, create);
}

//-----------------------------------------------------------------------------

void SliderBase::slider_changed_cb(lv_event_t* e)
{
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    SliderBase* sl = (SliderBase*)lv_event_get_user_data(e);
    if (sl != nullptr) {
      lv_obj_t* target = lv_event_get_target(e);
      sl->setValue(lv_slider_get_value(target));
    }
  }
}

SliderBase::SliderBase(Window* parent, coord_t width, coord_t height, int32_t vmin, int32_t vmax,
               std::function<int()> getValue,
               std::function<void(int)> setValue) :
    Window(parent, {0, 0, width, height}, window_create),
    vmin(vmin),
    vmax(vmax),
    _getValue(std::move(getValue)),
    _setValue(std::move(setValue))
{
}

void SliderBase::update()
{
  withLive([&](LiveWindow& live) {
    update(live);
  });
}

void SliderBase::update(Window::LiveWindow&)
{
  if (_getValue == nullptr) return;

  // Fix for lv_slider_set_value not working when using the rotary encoder to
  // update value
  slider.with([&](lv_obj_t* sliderObj) {
    auto bar = (lv_bar_t*)sliderObj;
    bar->cur_value_anim.anim_state = -1;
    bar->cur_value_anim.anim_end = _getValue();

    lv_slider_set_value(sliderObj, _getValue(), LV_ANIM_OFF);
  });
}

void SliderBase::onDelete()
{
  delete[] tickPts;
  tickPts = nullptr;
}

void SliderBase::setValue(int value)
{
  withLive([&](LiveWindow&) {
    if (_setValue != nullptr) _setValue(limit(vmin, value, vmax));
  });
}

void SliderBase::onLiveCheckEvents(Window::LiveWindow& live)
{
  Window::onLiveCheckEvents(live);
  if (_getValue != nullptr) {
    int v = _getValue();
    slider.with([&](lv_obj_t* sliderObj) {
      if (v != lv_slider_get_value(sliderObj)) update(live);
    });
  }
}

void SliderBase::enable(bool enabled)
{
  withLive([&](LiveWindow&) {
    slider.with([&](lv_obj_t* sliderObj) {
      if (lv_obj_has_state(sliderObj, LV_STATE_DISABLED) != enabled) return;
      if (enabled)
        lv_obj_clear_state(sliderObj, LV_STATE_DISABLED);
      else
        lv_obj_add_state(sliderObj, LV_STATE_DISABLED);
    });
  });
}

void SliderBase::setColor(LcdFlags color)
{
  slider.with([&](lv_obj_t* sliderObj) {
    etx_bg_color_from_flags(sliderObj, color, LV_PART_KNOB);
  });
  if (tickPts)
    for (int i = 0; i < (vmax - vmin - 1); i += 1)
      if (tickPts[i]) etx_bg_color_from_flags(tickPts[i], color);
}

//-----------------------------------------------------------------------------

// Slider
const lv_style_const_prop_t slider_knob_props[] = {
    LV_STYLE_CONST_PAD_TOP(PAD_LARGE + 1),  LV_STYLE_CONST_PAD_BOTTOM(PAD_LARGE + 1),
    LV_STYLE_CONST_PAD_LEFT(PAD_SMALL), LV_STYLE_CONST_PAD_RIGHT(PAD_SMALL),
    LV_STYLE_CONST_RADIUS(PAD_SMALL),   LV_STYLE_CONST_BORDER_WIDTH(PAD_TINY),
    LV_STYLE_PROP_INV,
};
LV_STYLE_CONST_MULTI_INIT(slider_knob, slider_knob_props);

static void slider_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
  etx_solid_bg(obj, COLOR_THEME_SECONDARY1_INDEX);
  etx_obj_add_style(obj, styles->rounded, LV_PART_MAIN);
  etx_bg_color(obj, COLOR_THEME_PRIMARY2_INDEX,
               LV_PART_MAIN | LV_STATE_FOCUSED);
  etx_bg_color(obj, COLOR_THEME_PRIMARY2_INDEX,
               LV_PART_MAIN | LV_STATE_FOCUSED | LV_STATE_EDITED);

  etx_bg_color(obj, COLOR_THEME_PRIMARY1_INDEX,
               LV_PART_INDICATOR | LV_STATE_FOCUSED);
  etx_bg_color(obj, COLOR_THEME_PRIMARY1_INDEX,
               LV_PART_INDICATOR | LV_STATE_FOCUSED | LV_STATE_EDITED);

  etx_solid_bg(obj, COLOR_THEME_PRIMARY2_INDEX, LV_PART_KNOB);
  etx_obj_add_style(obj, slider_knob, LV_PART_KNOB);
  etx_obj_add_style(obj, styles->border_color[COLOR_THEME_SECONDARY1_INDEX], LV_PART_MAIN);
  etx_obj_add_style(obj, styles->border_color[COLOR_THEME_PRIMARY1_INDEX],
                    LV_PART_KNOB | LV_STATE_FOCUSED);
  etx_obj_add_style(obj, styles->state_focus_frame,
                    LV_PART_KNOB | LV_STATE_FOCUSED);
  etx_solid_bg(obj, COLOR_THEME_PRIMARY2_INDEX,
               LV_PART_KNOB | LV_STATE_FOCUSED | LV_STATE_EDITED);
  etx_obj_add_style(obj, styles->border_color[COLOR_THEME_PRIMARY1_INDEX],
                    LV_PART_KNOB | LV_STATE_FOCUSED | LV_STATE_EDITED);
  etx_obj_add_style(obj, styles->state_edit_frame,
                    LV_PART_KNOB | LV_STATE_FOCUSED | LV_STATE_EDITED);

  etx_obj_add_style(obj, styles->disabled, LV_PART_MAIN | LV_STATE_DISABLED);
  etx_obj_add_style(obj, styles->disabled, LV_PART_KNOB | LV_STATE_DISABLED);
  etx_obj_add_style(obj, styles->disabled, LV_PART_INDICATOR | LV_STATE_DISABLED);
}

static const lv_obj_class_t slider_class = {
    .base_class = &lv_slider_class,
    .constructor_cb = slider_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = 0,
    .height_def = PAD_LARGE,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(lv_slider_t),
};

static lv_obj_t* slider_create(lv_obj_t* parent)
{
  return etx_create(&slider_class, parent);
}

Slider::Slider(Window* parent, coord_t width, int32_t vmin, int32_t vmax,
               std::function<int()> getValue,
               std::function<void(int)> setValue) :
    SliderBase(parent, width, EdgeTxStyles::UI_ELEMENT_HEIGHT, vmin, vmax, getValue, setValue)
{
  padTop(PAD_LARGE + 1);
  padLeft(PAD_LARGE);
  padRight(PAD_LARGE);

  auto sliderField = createSliderFormField(this, slider_create);
  if (!sliderField) {
    failClosed();
    return;
  }
  if (!sliderField->withLive(
          [&](LiveWindow& live) { return requireLvObj(slider, live.lvobj()); })) {
    delete sliderField;
    return;
  }
  slider.with([](lv_obj_t* sliderObj) {
    lv_obj_set_width(sliderObj, lv_pct(100));
  });

  slider.with([&](lv_obj_t* sliderObj) {
    lv_obj_add_event_cb(sliderObj, SliderBase::slider_changed_cb,
                        LV_EVENT_VALUE_CHANGED, this);
    lv_slider_set_range(sliderObj, vmin, vmax);
  });

  delayLoad();

  int range = vmax - vmin;
  if (range > 1 && range < 10) {
    tickPts = new (std::nothrow) lv_obj_t*[range - 1];
    if (tickPts) {
      for (int n = 1; n < range; n += 1) {
        lv_obj_t* p = nullptr;
        withLive([&](LiveWindow& live) { p = lv_obj_create(live.lvobj()); });
        if (p) {
          lv_obj_set_size(p, PAD_TINY, PAD_MEDIUM);
          etx_solid_bg(p, COLOR_THEME_PRIMARY2_INDEX);
        }
        tickPts[n - 1] = p;
      }
    }
  }

  update();
}

void Slider::delayedInit()
{
  coord_t w = 0;
  withLive([&](LiveWindow& live) {
    w = lv_obj_get_width(live.lvobj()) - PAD_LARGE * 2;
  });
  coord_t x = -1;
  int range = vmax - vmin;
  if (tickPts && range > 1 && range < 10) {
    for (int n = 1; n < range; n += 1) {
      if (tickPts[n - 1])
        lv_obj_set_pos(tickPts[n - 1], x + (w * n) / range, 1);
    }
  }
}

//-----------------------------------------------------------------------------

// Vertical Slider
const lv_style_const_prop_t vslider_knob_props[] = {
    LV_STYLE_CONST_PAD_TOP(PAD_SMALL), LV_STYLE_CONST_PAD_BOTTOM(PAD_SMALL),
    LV_STYLE_CONST_PAD_LEFT(PAD_LARGE + 1),  LV_STYLE_CONST_PAD_RIGHT(PAD_LARGE + 1),
    LV_STYLE_CONST_RADIUS(PAD_SMALL),   LV_STYLE_CONST_BORDER_WIDTH(PAD_TINY),
    LV_STYLE_PROP_INV,
};
LV_STYLE_CONST_MULTI_INIT(vslider_knob, vslider_knob_props);

static void vslider_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
  etx_solid_bg(obj, COLOR_THEME_SECONDARY1_INDEX);
  etx_obj_add_style(obj, styles->rounded, LV_PART_MAIN);
  etx_bg_color(obj, COLOR_THEME_PRIMARY2_INDEX,
               LV_PART_MAIN | LV_STATE_FOCUSED);
  etx_bg_color(obj, COLOR_THEME_PRIMARY2_INDEX,
               LV_PART_MAIN | LV_STATE_FOCUSED | LV_STATE_EDITED);

  etx_bg_color(obj, COLOR_THEME_PRIMARY1_INDEX,
               LV_PART_INDICATOR | LV_STATE_FOCUSED);
  etx_bg_color(obj, COLOR_THEME_PRIMARY1_INDEX,
               LV_PART_INDICATOR | LV_STATE_FOCUSED | LV_STATE_EDITED);

  etx_obj_add_style(obj, styles->bg_opacity_cover, LV_PART_KNOB);
  etx_obj_add_style(obj, vslider_knob, LV_PART_KNOB);
  etx_obj_add_style(obj, styles->border_color[COLOR_THEME_SECONDARY1_INDEX], LV_PART_MAIN);
  etx_obj_add_style(obj, styles->border_color[COLOR_THEME_PRIMARY1_INDEX],
                    LV_PART_KNOB | LV_STATE_FOCUSED);
  etx_obj_add_style(obj, styles->state_focus_frame,
                    LV_PART_KNOB | LV_STATE_FOCUSED);
  etx_solid_bg(obj, COLOR_THEME_PRIMARY2_INDEX,
               LV_PART_KNOB | LV_STATE_FOCUSED | LV_STATE_EDITED);
  etx_obj_add_style(obj, styles->border_color[COLOR_THEME_PRIMARY1_INDEX],
                    LV_PART_KNOB | LV_STATE_FOCUSED | LV_STATE_EDITED);
  etx_obj_add_style(obj, styles->state_edit_frame,
                    LV_PART_KNOB | LV_STATE_FOCUSED | LV_STATE_EDITED);

  etx_obj_add_style(obj, styles->disabled, LV_PART_MAIN | LV_STATE_DISABLED);
  etx_obj_add_style(obj, styles->disabled, LV_PART_KNOB | LV_STATE_DISABLED);
  etx_obj_add_style(obj, styles->disabled, LV_PART_INDICATOR | LV_STATE_DISABLED);
}

static const lv_obj_class_t vslider_class = {
    .base_class = &lv_slider_class,
    .constructor_cb = vslider_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = PAD_LARGE,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(lv_slider_t),
};

static lv_obj_t* vslider_create(lv_obj_t* parent)
{
  return etx_create(&vslider_class, parent);
}

VerticalSlider::VerticalSlider(Window* parent, coord_t height, int32_t vmin, int32_t vmax,
               std::function<int()> getValue,
               std::function<void(int)> setValue) :
    SliderBase(parent, EdgeTxStyles::UI_ELEMENT_HEIGHT, height, vmin,vmax, getValue, setValue)
{
  padLeft(PAD_LARGE + 1);
  padTop(PAD_LARGE);
  padBottom(PAD_LARGE);

  auto sliderField = createSliderFormField(this, vslider_create);
  if (!sliderField) {
    failClosed();
    return;
  }
  if (!sliderField->withLive(
          [&](LiveWindow& live) { return requireLvObj(slider, live.lvobj()); })) {
    delete sliderField;
    return;
  }
  slider.with([](lv_obj_t* sliderObj) {
    lv_obj_set_height(sliderObj, lv_pct(100));
  });

  slider.with([&](lv_obj_t* sliderObj) {
    lv_obj_add_event_cb(sliderObj, SliderBase::slider_changed_cb,
                        LV_EVENT_VALUE_CHANGED, this);
    lv_slider_set_range(sliderObj, vmin, vmax);
  });

  delayLoad();

  int range = vmax - vmin;
  if (range > 1 && range < 10) {
    tickPts = new (std::nothrow) lv_obj_t*[range - 1];
    if (tickPts) {
      for (int n = 1; n < range; n += 1) {
        lv_obj_t* p = nullptr;
        withLive([&](LiveWindow& live) { p = lv_obj_create(live.lvobj()); });
        if (p) {
          lv_obj_set_size(p, PAD_MEDIUM, PAD_TINY);
          etx_solid_bg(p, COLOR_THEME_PRIMARY2_INDEX);
        }
        tickPts[n - 1] = p;
      }
    }
  }

  update();
}

#if defined(SIMU)
bool sliderFormFieldCreateFailureFailsClosedForTest()
{
  class TestSlider : public Slider
  {
   public:
    TestSlider(Window* parent) :
        Slider(parent, 100, 0, 5, []() { return 3; }, [](int) {})
    {
    }

    bool hasSlider() const { return slider.isPresentForTest(); }
  };

  sliderForceFormFieldCreateFailureForTest(true);
  auto slider = new TestSlider(MainWindow::instance());
  sliderForceFormFieldCreateFailureForTest(false);

  if (!slider) return false;
  slider->update();
  return !slider->hasSlider() && !slider->isAvailable() &&
         !slider->isVisible();
}
#endif

void VerticalSlider::delayedInit()
{
  coord_t h = 0;
  withLive([&](LiveWindow& live) {
    h = lv_obj_get_height(live.lvobj()) - PAD_LARGE * 2;
  });
  coord_t y = -1;
  int range = vmax - vmin;
  if (tickPts && range > 1 && range < 10) {
    for (int n = 1; n < range; n += 1) {
      if (tickPts[n - 1])
        lv_obj_set_pos(tickPts[n - 1], 1, y + (h * n) / range);
    }
  }
}
