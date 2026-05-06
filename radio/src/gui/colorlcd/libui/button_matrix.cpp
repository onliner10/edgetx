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

#include "button_matrix.h"

#include <stdlib.h>
#include <string.h>

#include "etx_lv_theme.h"
#include "mainwindow.h"

static void btnmatrix_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj)
{
  etx_obj_add_style(obj, styles->rounded, LV_PART_MAIN);
  etx_obj_add_style(obj, styles->bg_opacity_20, LV_PART_MAIN | LV_STATE_EDITED);

  etx_solid_bg(obj, COLOR_THEME_PRIMARY2_INDEX,
               LV_PART_MAIN | LV_STATE_FOCUSED);
  etx_obj_add_style(obj, styles->border_color[COLOR_THEME_PRIMARY1_INDEX],
                    LV_PART_MAIN | LV_STATE_FOCUSED);
  etx_obj_add_style(obj, styles->state_focus_frame,
                    LV_PART_MAIN | LV_STATE_FOCUSED);

  etx_std_style(obj, LV_PART_ITEMS, PAD_LARGE);
  etx_remove_border_color(obj, LV_PART_ITEMS | LV_STATE_FOCUSED);

  etx_obj_add_style(obj, styles->border_color[COLOR_THEME_PRIMARY1_INDEX],
                    LV_PART_ITEMS | LV_STATE_EDITED);
}

static const lv_obj_class_t btnmatrix_class = {
    .base_class = &lv_btnmatrix_class,
    .constructor_cb = btnmatrix_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = 0,
    .height_def = 0,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_INHERIT,
    .instance_size = sizeof(lv_btnmatrix_t),
};

static lv_obj_t* btnmatrix_create(lv_obj_t* parent)
{
  return etx_create(&btnmatrix_class, parent);
}

static const char _filler[] = "0";
static const char _newline[] = "\n";
static const char _map_end[] = "";
static const char* _empty_map[] = {_map_end};

static void btn_matrix_event(lv_event_t* e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t* obj = lv_event_get_target(e);
    auto btn_id = *((uint8_t*)lv_event_get_param(e));
    auto btnm = (ButtonMatrix*)lv_event_get_user_data(e);
    if (!btnm) return;

    bool edited = lv_obj_has_state(obj, LV_STATE_EDITED);
    bool is_pointer =
        lv_indev_get_type(lv_indev_get_act()) == LV_INDEV_TYPE_POINTER;
    if (edited || is_pointer) {
      btnm->onPress(btn_id);
    }
  }
}

ButtonMatrix::ButtonMatrix(Window* parent, const rect_t& r) :
    FormField(parent, r, btnmatrix_create)
{
  withLive([](LiveWindow& live) {
    lv_obj_add_flag(live.lvobj(), LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  });
  setWindowFlag(NO_FOCUS);

  withLive([&](LiveWindow& live) {
    lv_obj_add_event_cb(live.lvobj(), btn_matrix_event, LV_EVENT_VALUE_CHANGED,
                        this);
  });
}

ButtonMatrix::~ButtonMatrix() { deallocate(); }

void ButtonMatrix::deallocate()
{
  if (txt_cnt == 0) return;

  for (uint8_t i = 0; i < txt_cnt; i++) {
    char* txt = lv_btnm_map[i];
    if (txt != _filler && txt != _newline && txt != _map_end) free(txt);
  }

  free(lv_btnm_map);
  free(txt_index);

  txt_cnt = 0;
  btn_cnt = 0;
}

void ButtonMatrix::initBtnMap(uint8_t cols, uint8_t btns)
{
  deallocate();
  withLive([&](LiveWindow& live) {
    auto obj = live.lvobj();

    if (cols == 0 || btns == 0) {
      lv_btnmatrix_set_map(obj, _empty_map);
      return;
    }

    uint8_t rows = ((btns - 1) / cols) + 1;
    if (rows == 1) cols = btns;
    uint16_t btnCount = uint16_t(cols) * rows;
    uint16_t txtCount = uint16_t(cols + 1) * rows;
    if (btnCount > UINT8_MAX || txtCount > UINT8_MAX) {
      lv_btnmatrix_set_map(obj, _empty_map);
      return;
    }

    lv_btnm_map = (char**)malloc(sizeof(char*) * txtCount);
    txt_index = (uint8_t*)malloc(sizeof(uint8_t) * cols * rows);
    if (!lv_btnm_map || !txt_index) {
      free(lv_btnm_map);
      free(txt_index);
      lv_btnm_map = nullptr;
      txt_index = nullptr;
      lv_btnmatrix_set_map(obj, _empty_map);
      return;
    }

    txt_cnt = uint8_t(txtCount);
    btn_cnt = btns;

    uint8_t col = 0;
    uint8_t btn = 0;
    uint8_t txt_i = 0;

    while (btn < btnCount) {
      if (col == cols) {
        lv_btnm_map[txt_i++] = (char*)_newline;
        col = 0;
      }

      txt_index[btn] = txt_i;
      lv_btnm_map[txt_i++] = (char*)_filler;
      btn++;
      col++;
    }
    lv_btnm_map[txt_i] = (char*)_map_end;
    update();
  });
}

void ButtonMatrix::setText(uint8_t btn_id, const char* txt)
{
  withLive([&](LiveWindow&) {
    if (btn_id >= btn_cnt || !lv_btnm_map || !txt_index) return;

    char* copy = strdup(txt);
    if (copy) lv_btnm_map[txt_index[btn_id]] = copy;
  });
}

void ButtonMatrix::update()
{
  withLive([&](LiveWindow& live) {
    auto obj = live.lvobj();

    if (!lv_btnm_map) {
      lv_btnmatrix_set_map(obj, _empty_map);
      return;
    }
    lv_btnmatrix_set_map(obj, (const char**)lv_btnm_map);
    lv_btnmatrix_set_btn_ctrl_all(
        obj, LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT);
    int btn = 0;
    for (int i = 0; lv_btnm_map[i] != _map_end; i += 1) {
      if (lv_btnm_map[i] == _filler)
        lv_btnmatrix_set_btn_ctrl(obj, btn, LV_BTNMATRIX_CTRL_HIDDEN);
      else
        lv_btnmatrix_clear_btn_ctrl(obj, btn, LV_BTNMATRIX_CTRL_HIDDEN);
      if (lv_btnm_map[i] != _newline) btn += 1;
    }
  });
}

void ButtonMatrix::onLiveClicked(Window::LiveWindow& live)
{
  lv_group_focus_obj(live.lvobj());
  setEditMode(true);
}

void ButtonMatrix::setChecked(uint8_t btn_id)
{
  withLive([&](LiveWindow& live) {
    auto obj = live.lvobj();

    if (isActive(btn_id))
      lv_btnmatrix_set_btn_ctrl(obj, btn_id, LV_BTNMATRIX_CTRL_CHECKED);
    else
      lv_btnmatrix_clear_btn_ctrl(obj, btn_id, LV_BTNMATRIX_CTRL_CHECKED);
  });
}

#if defined(SIMU)
void etxCreateForceObjectAllocationFailureForTest(bool force);

bool buttonMatrixObjectAllocationFailureFailsClosedForTest()
{
  class TestButtonMatrix : public ButtonMatrix
  {
   public:
    explicit TestButtonMatrix(Window* parent) :
        ButtonMatrix(parent, {0, 0, 100, 40})
    {
    }

    void exercise()
    {
      initBtnMap(2, 3);
      setText(0, "A");
      update();
      onClicked();
      setChecked(0);
    }
  };

  etxCreateForceObjectAllocationFailureForTest(true);
  auto matrix = new (std::nothrow) TestButtonMatrix(MainWindow::instance());
  etxCreateForceObjectAllocationFailureForTest(false);

  if (!matrix) return false;
  matrix->exercise();

  bool ok = !matrix->isAvailable() && !matrix->isVisible() &&
            !matrix->automationClickable();
  delete matrix;
  return ok;
}
#endif
