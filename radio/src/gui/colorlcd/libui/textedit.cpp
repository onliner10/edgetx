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

#include "textedit.h"

#include "keyboard_text.h"
#include "mainwindow.h"
#include "myeeprom.h"
#include "storage/storage.h"
#include "etx_lv_theme.h"

#include <new>

#if defined(HARDWARE_KEYS)
#include "menu.h"
#endif

class TextArea : public FormField
{
 public:
  TextArea(Window* parent, const rect_t& rect, char* value, uint8_t length) :
      FormField(parent, rect, etx_textarea_create), value(value), length(length)
  {
    setWindowFlag(NO_FOCUS);
    if (!hasLvObj()) return;

    lv_textarea_set_max_length(lvobj, length);
    lv_textarea_set_placeholder_text(lvobj, "---");

    setFocusHandler([=](bool focus) {
      if (!focus && editMode) {
        setEditMode(false);
        hide();
        if (auto parentObj = parent ? parent->getLvObj() : nullptr) {
          lv_group_focus_obj(parentObj);
          lv_obj_clear_state(parentObj, LV_STATE_FOCUSED);
        }
      }
    });

    update();
  }

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "TextArea"; }
#endif

  void update()
  {
    // value may not be null-terminated
    std::string txt(value, length);
    withAvailableLvObj([&](lv_obj_t* obj) {
      lv_textarea_set_text(obj, txt.c_str());
    });
  }

  void onClicked() override {
    if (!acceptsEvents()) return;
    setEditMode(true);
  }

  void openKeyboard() {
    if (!acceptsEvents()) return;
    TextKeyboard::open(this);
  }

  void setCancelHandler(std::function<void(void)> handler)
  {
    cancelHandler = std::move(handler);
  }

 protected:
  char* value;
  uint8_t length;
  std::function<void(void)> cancelHandler = nullptr;

  void trim()
  {
    for (int i = length - 1; i >= 0; i--) {
      if (value[i] == ' ' || value[i] == '\0')
        value[i] = '\0';
      else
        break;
    }
  }

  void changeEnd(bool forceChanged = false) override
  {
    bool changed = false;
    const char* text = nullptr;
    if (!withAvailableLvObj([&](lv_obj_t* obj) {
          text = lv_textarea_get_text(obj);
        }) ||
        !text) {
      return;
    }

    if (strncmp(value, text, length) != 0) {
      changed = true;
    }

    if (changed || forceChanged) {
      strncpy(value, text, length);
      trim();
      FormField::changeEnd();
    } else if (cancelHandler) {
      cancelHandler();
    }
  }

  void onCancel() override
  {
    if (cancelHandler)
      cancelHandler();
    else
      FormField::onCancel();
  }
};

/*
  The lv_textarea object is slow. To avoid too much overhead on views with multiple
  edit fields, the text area is initially displayed as a button. When the button
  is pressed, a text area object is created over the top of the button in order
  to edit the value.
*/
TextEdit::TextEdit(Window* parent, const rect_t& rect, char* text,
                               uint8_t length,
                               std::function<void(void)> updateHandler) :
    TextButton(parent, rect, "", [=]() {
      openEdit();
      return 0;
    }),
    updateHandler(updateHandler), text(text), length(length)
{
  if (rect.w == 0) setWidth(EdgeTxStyles::EDIT_FLD_WIDTH);

  if (!isAvailable() || !label) return;
  update();
  lv_obj_align(label, LV_ALIGN_OUT_LEFT_MID, 0, PAD_TINY);
}

void TextEdit::update()
{
  if (text[0]) {
    std::string s(text, length);
    setText(s);
  } else {
    setText("---");
  }
}

void TextEdit::openEdit()
{
  if (!acceptsEvents()) return;

  if (edit == nullptr) {
    auto newEdit = Window::makeLive<TextArea>(
        this, rect_t{0, 0, width(), height()}, text, length);
    if (!newEdit) return;
    edit = newEdit;
    edit->setChangeHandler([=]() {
      update();
      if (updateHandler) updateHandler();
      withAvailableLvObj([](lv_obj_t* obj) {
        lv_group_focus_obj(obj);
      });
      edit->hide();
    });
    edit->setCancelHandler([=]() {
      withAvailableLvObj([](lv_obj_t* obj) {
        lv_group_focus_obj(obj);
      });
      edit->hide();
    });
  }
  if (!syncOverlay(edit)) return;
  edit->show();
  if (auto editObj = edit->getLvObj()) {
    lv_group_focus_obj(editObj);
    lv_obj_add_state(editObj, LV_STATE_FOCUSED | LV_STATE_EDITED);
  }
  edit->openKeyboard();
}

void TextEdit::preview(bool edited, char* text, uint8_t length)
{
  setWindowFlag(NO_FOCUS | NO_CLICK);

  edit = Window::makeLive<TextArea>(
      this, rect_t{0, 0, width(), height()}, text, length);
  if (!edit) return;
  edit->setWindowFlag(NO_CLICK);
  if (auto editObj = edit->getLvObj()) {
    lv_group_focus_obj(editObj);
    lv_obj_add_state(editObj, LV_STATE_FOCUSED);
    if (edited) lv_obj_add_state(editObj, LV_STATE_EDITED);
  }
}

ModelTextEdit::ModelTextEdit(Window* parent, const rect_t& rect, char* value,
                             uint8_t length, std::function<void(void)> updateHandler) :
    TextEdit(parent, rect, value, length,
             [=]() {
               if (updateHandler) updateHandler();
               storageDirty(EE_MODEL);
             })
{
}

ModelStringEdit::ModelStringEdit(Window* parent, const rect_t& rect, std::string value,
                                 std::function<void(const char* s)> updateHandler) :
    TextEdit(parent, rect, txt, MAX_STR_EDIT_LEN,
             [=]() {
               if (updateHandler) updateHandler(txt);
               storageDirty(EE_MODEL);
             })
{
  strncpy(txt, value.c_str(), length);
  update();
}

RadioTextEdit::RadioTextEdit(Window* parent, const rect_t& rect, char* value,
                             uint8_t length) :
    TextEdit(parent, rect, value, length,
             []() { storageDirty(EE_GENERAL); })
{
}

#if defined(SIMU)
void etxCreateForceObjectAllocationFailureForTest(bool force);

bool textEditTextAreaAllocationFailureDoesNotCacheDeadEditorForTest()
{
  class TestTextEdit : public TextEdit
  {
   public:
    TestTextEdit(Window* parent, char* value, uint8_t length) :
        TextEdit(parent, {0, 0, 120, EdgeTxStyles::UI_ELEMENT_HEIGHT}, value,
                 length)
    {
    }

    void exerciseOpenEdit() { openEdit(); }

    bool hasCachedEditor() const { return edit != nullptr; }
  };

  char value[8] = "Model";
  auto textEdit =
      new (std::nothrow) TestTextEdit(MainWindow::instance(), value, sizeof(value));
  if (!textEdit || !textEdit->isAvailable()) {
    delete textEdit;
    return false;
  }

  etxCreateForceObjectAllocationFailureForTest(true);
  textEdit->exerciseOpenEdit();
  etxCreateForceObjectAllocationFailureForTest(false);

  bool ok = textEdit->isAvailable() && textEdit->automationClickable() &&
            !textEdit->hasCachedEditor();
  delete textEdit;
  return ok;
}
#endif
