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

#include "numberedit.h"

#include <new>

#include "audio.h"
#include "debug.h"
#include "etx_lv_theme.h"
#include "hal/rotary_encoder.h"
#include "keyboard_number.h"
#include "keys.h"
#include "mainwindow.h"
#include "strhelpers.h"

class NumberArea : public FormField
{
 public:
  NumberArea(NumberEdit* parent, const rect_t& rect) :
      FormField(parent, rect, etx_textarea_create), numEdit(parent)
  {
    setWindowFlag(NO_FOCUS);

    withLive([&](lv_obj_t* obj) {
      if (parent->getTextFlags() & CENTERED)
        etx_obj_add_style(obj, styles->text_align_center, LV_PART_MAIN);
      else
        etx_obj_add_style(obj, styles->text_align_right, LV_PART_MAIN);

      // Allow encoder acceleration
      lv_obj_add_flag(obj, LV_OBJ_FLAG_ENCODER_ACCEL);

      lv_obj_add_event_cb(obj, NumberArea::numberedit_cb, LV_EVENT_KEY, this);
    });

    setFocusHandler([=](bool focus) {
      if (!focus && editMode) {
        setEditMode(false);
        hide();
        if (parent) {
          parent->withLive([](Window::LiveWindow& liveParent) {
            lv_obj_clear_state(liveParent.lvobj(), LV_STATE_FOCUSED);
          });
        }
      }
    });

    update();
  }

#if defined(DEBUG_WINDOWS)
  std::string getName() const override
  {
    if (numEdit)
      return "NumberArea(" + numEdit->getName() + ")";
    else
      return "NumberArea(unknown)";
  }
#endif

  void onLiveEvent(LiveWindow& live, event_t event) override
  {
    TRACE_WINDOWS("%s received event 0x%X", getWindowDebugString().c_str(),
                  event);

    if (editMode) {
      int value = numEdit->getValue();
      switch (event) {
#if defined(HARDWARE_KEYS)
        case EVT_ROTARY_RIGHT: {
          auto step = numEdit->step;
          step += (rotaryEncoderGetAccel() * numEdit->accelFactor) / 8;
          do {
#if defined(USE_HATS_AS_KEYS)
            value -= step;
#else
            value += step;
#endif
          } while (numEdit->isValueAvailable &&
                   !numEdit->isValueAvailable(value) && value <= numEdit->vmax);
          if (value <= numEdit->vmax) {
            numEdit->setValue(value);
          } else {
            numEdit->setValue(numEdit->vmax);
            onKeyError();
          }
          return;
        }

        case EVT_ROTARY_LEFT: {
          auto step = numEdit->step;
          step += (rotaryEncoderGetAccel() * numEdit->accelFactor) / 8;
          do {
#if defined(USE_HATS_AS_KEYS)
            value += step;
#else
            value -= step;
#endif
          } while (numEdit->isValueAvailable &&
                   !numEdit->isValueAvailable(value) && value >= numEdit->vmin);
          if (value >= numEdit->vmin) {
            numEdit->setValue(value);
          } else {
            numEdit->setValue(numEdit->vmin);
            onKeyError();
          }
          return;
        }
#endif

        case EVT_VIRTUAL_KEY_PLUS:
          numEdit->setValue(value + numEdit->step);
          break;

        case EVT_VIRTUAL_KEY_MINUS:
          numEdit->setValue(value - numEdit->step);
          break;

        case EVT_VIRTUAL_KEY_FORWARD:
          numEdit->setValue(value + numEdit->fastStep * numEdit->step);
          break;

        case EVT_VIRTUAL_KEY_BACKWARD:
          numEdit->setValue(value - numEdit->fastStep * numEdit->step);
          break;

        case EVT_VIRTUAL_KEY_DEFAULT:
          numEdit->setValue(numEdit->vdefault);
          break;

        case EVT_VIRTUAL_KEY_MAX:
          numEdit->setValue(numEdit->vmax);
          break;

        case EVT_VIRTUAL_KEY_MIN:
          numEdit->setValue(numEdit->vmin);
          break;

        case EVT_VIRTUAL_KEY_SIGN:
          numEdit->setValue(-value);
          break;
      }
    }

    FormField::onLiveEvent(live, event);
  }

  void onLiveClicked(LiveWindow&) override
  {
    lv_indev_type_t indev_type = lv_indev_get_type(lv_indev_get_act());
    if (indev_type == LV_INDEV_TYPE_POINTER) {
      setEditMode(true);
    } else {
      FormField::onClicked();
      if (!editMode) changeEnd();
    }
  }

  void openKeyboard()
  {
    editTextIsRaw = numEdit->useDirectKeyboard();
    if (!withLive([&](LiveWindow& live) {
          lv_textarea_set_text(
              live.lvobj(), editTextIsRaw ? numEdit->getEditVal().c_str()
                                          : numEdit->getDisplayVal().c_str());
        }))
      return;
    NumberKeyboard::open(this, numEdit);
  }

  void directEdit()
  {
    editTextIsRaw = false;
    FormField::onClicked();
  }

  void update()
  {
    withLive([&](LiveWindow& live) {
      lv_textarea_set_text(live.lvobj(), numEdit->getDisplayVal().c_str());
    });
  }

 protected:
  NumberEdit* numEdit = nullptr;
  bool editTextIsRaw = false;

  void changeEnd(bool forceChanged = false) override
  {
    if (editTextIsRaw) {
      std::string text;
      if (!withLive([&](LiveWindow& live) {
            auto rawText = lv_textarea_get_text(live.lvobj());
            text = rawText ? rawText : "";
          }))
        return;
      numEdit->setValueFromEditVal(text.c_str());
      editTextIsRaw = false;
    }
    FormField::changeEnd(forceChanged);
  }

  void onCancel() override { onClicked(); }

  static void numberedit_cb(lv_event_t* e)
  {
    auto numEdit = static_cast<NumberArea*>(
        Window::fromAvailableLvObj(lv_event_get_target(e)));
    if (!numEdit) return;

    uint32_t key = lv_event_get_key(e);
    switch (key) {
      case LV_KEY_LEFT:
        numEdit->onEvent(EVT_ROTARY_LEFT);
        break;
      case LV_KEY_RIGHT:
        numEdit->onEvent(EVT_ROTARY_RIGHT);
        break;
    }
  }
};

/*
  The lv_textarea object is slow. To avoid too much overhead on views with
  multiple edit fields, the text area is initially displayed as a button. When
  the button is pressed, a text area object is created over the top of the
  button in order to edit the value.
*/
NumberEdit::NumberEdit(Window* parent, const rect_t& rect, int vmin, int vmax,
                       std::function<int()> getValue,
                       std::function<void(int)> setValue, LcdFlags textFlags) :
    TextButton(parent, rect, "",
               [=]() {
                 openEdit();
                 return 0;
               }),
    _getValue(std::move(getValue)),
    _setValue(std::move(setValue)),
    vmin(vmin),
    vmax(vmax)
{
  if (parent) {
    const char* title = parent->getFormFieldTitle();
    if (title) editTitle = title;
  }

  if (rect.w == 0 || rect.w == LV_SIZE_CONTENT)
    setWidth(EdgeTxStyles::EDIT_FLD_WIDTH);

  setTextFlag(textFlags);

  padLeft(PAD_MEDIUM);
  padRight(PAD_SMALL);

  label.with([&](lv_obj_t* obj) {
    lv_obj_set_width(obj, LV_PCT(100));

    if (textFlags & CENTERED)
      etx_obj_add_style(obj, styles->text_align_center, LV_PART_MAIN);
    else
      etx_obj_add_style(obj, styles->text_align_right, LV_PART_MAIN);
  });

  update();
}

void NumberEdit::openEdit()
{
  if (edit == nullptr) {
    auto newEdit =
        Window::makeLive<NumberArea>(this, rect_t{0, 0, width(), height()});
    if (!newEdit) return;
    edit = newEdit;
    edit->setChangeHandler([=]() {
      update();
      if (onEdited) onEdited(currentValue);
      if (edit->hasFocus()) focus();
      edit->hide();
    });
  }
  if (!syncOverlay(edit)) return;
  edit->update();
  edit->show();
  if (edit->focus()) {
    edit->withLive([](Window::LiveWindow& liveEdit) {
      lv_obj_add_state(liveEdit.lvobj(), LV_STATE_FOCUSED | LV_STATE_EDITED);
    });
  }
  lv_indev_type_t indev_type = lv_indev_get_type(lv_indev_get_act());
  if (indev_type == LV_INDEV_TYPE_POINTER) {
    edit->openKeyboard();
  } else {
    edit->directEdit();
  }
}

void NumberEdit::update()
{
  if (_getValue == nullptr) return;
  currentValue = _getValue();
  updateDisplay();
}

std::string NumberEdit::getDisplayVal() const
{
  std::string str;
  if (displayFunction != nullptr) {
    str = displayFunction(currentValue);
  } else if (!zeroText.empty() && currentValue == 0) {
    str = zeroText;
  } else {
    str = formatNumberAsString(currentValue, textFlags, 0, prefix.c_str(),
                               suffix.c_str());
  }
  return str;
}

bool NumberEdit::hasDecimalPrecision() const
{
  return ((textFlags & PREC2) == PREC2) || (textFlags & PREC1);
}

bool NumberEdit::useDirectKeyboard() const { return directKeyboard; }

static int getNumberEditPrecisionScale(LcdFlags flags)
{
  if ((flags & PREC2) == PREC2) return 100;
  if (flags & PREC1) return 10;
  return 1;
}

std::string NumberEdit::getEditVal() const
{
  int scale = getNumberEditPrecisionScale(textFlags);
  int value = currentValue;

  if (scale == 1) {
    return std::to_string(value);
  }

  bool negative = value < 0;
  int64_t absValue = negative ? -(int64_t)value : value;
  int64_t whole = absValue / scale;
  int64_t fraction = absValue % scale;

  auto text = std::string(negative ? "-" : "") + std::to_string(whole) + ".";
  if (scale == 100 && fraction < 10) text += "0";
  text += std::to_string(fraction);
  return text;
}

void NumberEdit::setValueFromEditVal(const char* text)
{
  if (text == nullptr || *text == '\0') {
    setValue(0);
    return;
  }

  int scale = getNumberEditPrecisionScale(textFlags);
  bool negative = false;
  bool afterDecimal = false;
  int decimalDigits = 0;
  constexpr int64_t VALUE_LIMIT = 2147483647;
  int64_t value = 0;

  if (*text == '-' || *text == '+') {
    negative = *text == '-';
    text++;
  }

  while (*text != '\0') {
    if (*text == '.') {
      if (afterDecimal) break;
      afterDecimal = true;
      text++;
      continue;
    }

    if (*text >= '0' && *text <= '9') {
      if (!afterDecimal) {
        if (value < VALUE_LIMIT) {
          value = value * 10 + int64_t(*text - '0') * scale;
          if (value > VALUE_LIMIT) value = VALUE_LIMIT;
        }
      } else if (decimalDigits < (scale == 100 ? 2 : scale == 10 ? 1 : 0)) {
        int digitValue =
            (*text - '0') * (scale == 100 && decimalDigits == 0 ? 10 : 1);
        if (value <= VALUE_LIMIT - digitValue)
          value += digitValue;
        else
          value = VALUE_LIMIT;
        decimalDigits++;
      }
    }
    text++;
  }

  int32_t parsedValue = (int32_t)value;
  setValue(negative ? -parsedValue : parsedValue);
}

void NumberEdit::updateDisplay() { setText(getDisplayVal()); }

void NumberEdit::setValue(int value)
{
  auto newValue = limit(vmin, value, vmax);
  if (newValue != currentValue) {
    currentValue = newValue;
    if (_setValue != nullptr) {
      _setValue(currentValue);
    }
  }
  updateDisplay();
  if (edit) edit->update();
}

#if defined(SIMU)
void etxCreateForceObjectAllocationFailureForTest(bool force);

bool numberEditNumberAreaAllocationFailureDoesNotCacheDeadEditorForTest()
{
  class TestNumberEdit : public NumberEdit
  {
   public:
    TestNumberEdit(Window* parent, std::function<int()> getValue,
                   std::function<void(int)> setValue) :
        NumberEdit(parent, {0, 0, 120, EdgeTxStyles::UI_ELEMENT_HEIGHT}, 0, 100,
                   std::move(getValue), std::move(setValue))
    {
    }

    void exerciseOpenEdit() { openEdit(); }

    bool hasCachedEditor() const { return edit != nullptr; }
  };

  int value = 7;
  auto numberEdit = new (std::nothrow) TestNumberEdit(
      MainWindow::instance(), [&]() { return value; },
      [&](int newValue) { value = newValue; });
  if (!numberEdit || !numberEdit->isAvailable()) {
    delete numberEdit;
    return false;
  }

  etxCreateForceObjectAllocationFailureForTest(true);
  numberEdit->exerciseOpenEdit();
  etxCreateForceObjectAllocationFailureForTest(false);

  bool ok = numberEdit->isAvailable() && numberEdit->automationClickable() &&
            !numberEdit->hasCachedEditor();
  delete numberEdit;
  return ok;
}
#endif
