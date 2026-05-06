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

#include "model_mixes.h"

#include <algorithm>

#include "channel_bar.h"
#include "dialog.h"
#include "edgetx.h"
#include "menu.h"
#include "mixer_edit.h"
#include "mixes.h"
#include "toggleswitch.h"

#define SET_DIRTY()     storageDirty(EE_MODEL)

class MPlexIcon : public Window
{
 public:
  MPlexIcon(Window* parent, uint8_t index) :
    Window(parent, {0, 0, MPLEX_ICON_W, MPLEX_ICON_H}),
    index(index)
    {
    }

  void refresh()
  {
    MixData* mix = mixAddress(index);
    EdgeTxIcon n = ICON_MPLEX_ADD;
    if (mix->mltpx == MLTPX_MUL) {
      n = ICON_MPLEX_MULTIPLY;
    } else if (mix->mltpx == MLTPX_REPL) {
      n = ICON_MPLEX_REPLACE;
    }

    if (!icon) {
      icon = new StaticIcon(this, 0, 0, n, COLOR_THEME_SECONDARY1_INDEX);
      icon->center(width(), height());
    }

    icon->show(getLvIndex() != 0);
    icon->setIcon(n);
  }

  void setIndex(uint8_t i)
  {
    index = i;
  }

  static LAYOUT_VAL_SCALED(MPLEX_ICON_W, 25)
  static LAYOUT_VAL_SCALED(MPLEX_ICON_H, 29)

 protected:
  uint8_t index;
  StaticIcon* icon = nullptr;
};

class MixLineButton : public InputMixButtonBase
{
 public:
  MixLineButton(Window* parent, uint8_t index) :
    InputMixButtonBase(parent, index)
  {
    mplex = new MPlexIcon(parent, index);

    delayLoad();
  }

  void onDelete() override
  {
    if (mplex) mplex->deleteLater();
  }

  void delayedInit() override
  {
    refresh();
    ((InputMixGroupBase*)parent)->adjustHeight();
  }

  void onRefresh() override
  {
    check(isActive());

    const MixData& line = g_model.mixData[index];
    setWeight(line.weight, MIX_WEIGHT_MIN, MIX_WEIGHT_MAX);
    setSource(line.srcRaw);

    char tmp_str[64];
    char *s = tmp_str;
    *s = '\0';

    if (line.name[0]) {
      s = strAppend(s, line.name, LEN_EXPOMIX_NAME);
    }

    if (line.swtch) {
      if (tmp_str[0]) s = strAppend(s, " ");
      char* sw_pos = getSwitchPositionName(line.swtch);
      s = strAppend(s, sw_pos);
    }

    if (line.curve.value != 0) {
      if (tmp_str[0]) s = strAppend(s, " ");
      getCurveRefString(s, sizeof(tmp_str) - (s - tmp_str), line.curve);
    }

    setOpts(tmp_str);

    mplex->refresh();

    setFlightModes(line.flightModes);
  }

  void setIndex(uint8_t i) override
  {
    ListLineButton::setIndex(i);
    mplex->setIndex(i);
  }

  void updatePos(coord_t x, coord_t y) override
  {
    setPos(x, y);
    mplex->setPos(x - MPLEX_XO, y);
    mplex->show(y > ListLineButton::BTN_H);
  }

  static LAYOUT_VAL_SCALED(MPLEX_XO, 28)

 protected:
  MPlexIcon* mplex = nullptr;

  bool isActive() const override { return isMixActive(index); }
};

class MixGroup : public InputMixGroupBase
{
 public:
  MixGroup(Window* parent, mixsrc_t idx) :
    InputMixGroupBase(parent, idx)
  {
    adjustHeight();

    label.with([](lv_obj_t* obj) { lv_obj_set_pos(obj, PAD_TINY, -1); });

    lv_obj_t* chText = nullptr;
    if (idx >= MIXSRC_FIRST_CH && idx <= MIXSRC_LAST_CH &&
        g_model.limitData[idx - MIXSRC_FIRST_CH].name[0] != '\0') {
      withLive([&](LiveWindow& live) {
        chText = etx_label_create(live.lvobj(), FONT_XS_INDEX);
        char chanStr[10];
        char* s = strAppend(chanStr, STR_CH);
        strAppendUnsigned(s, idx - MIXSRC_FIRST_CH + 1);
        lv_label_set_text(chText, chanStr);
        lv_obj_set_pos(chText, PAD_TINY, CHNUM_Y - 1);
      });
    }

    refresh();
  }

  void enableMixerMonitor()
  {
    if (!monitor)
      monitor = new MixerChannelBar(this, {ListLineButton::GRP_W - CHBAR_W - PAD_LARGE, 1, CHBAR_W, CHBAR_H}, idx - MIXSRC_FIRST_CH);
    monitorVisible = true;
    monitor->show();
    adjustHeight();
  }

  void disableMixerMonitor()
  {
    monitorVisible = false;
    monitor->hide();
    adjustHeight();
  }

  void adjustHeight() override
  {
    _adjustHeight(monitorVisible ? CHNUM_Y : 0);
  }

  static LAYOUT_VAL_SCALED(CHNUM_Y, 17)
  static LAYOUT_VAL_SCALED(CHBAR_W, 100)
  static LAYOUT_VAL_SCALED(CHBAR_H, 14)

 protected:
  MixerChannelBar* monitor = nullptr;
  bool monitorVisible = false;
};

ModelMixesPage::ModelMixesPage(const PageDef& pageDef) : InputMixPageBase(pageDef)
{
}

bool ModelMixesPage::reachMixesLimit()
{
  if (getMixCount() >= MAX_MIXERS) {
    new MessageDialog(STR_WARNING, STR_NOFREEMIXER);
    return true;
  }
  return false;
}

InputMixGroupBase* ModelMixesPage::getGroupByIndex(uint8_t index)
{
  MixData* mix = mixAddress(index);
  if (is_memclear(mix, sizeof(MixData))) return nullptr;

  int ch = mix->destCh;
  return getGroupBySrc(MIXSRC_FIRST_CH + ch);
}

InputMixGroupBase* ModelMixesPage::createGroup(Window* form, mixsrc_t src)
{
  auto group = new MixGroup(form, src);
  if (showMonitors) group->enableMixerMonitor();
  return group;
}

InputMixButtonBase* ModelMixesPage::createLineButton(InputMixGroupBase *group, uint8_t index)
{
  auto button = new MixLineButton(group, index);

  lines.emplace_back(button);
  group->addLine(button);

  uint8_t ch = group->getMixSrc() - MIXSRC_FIRST_CH;
  button->setPressHandler([=]() -> uint8_t {
    Menu *menu = new Menu();
    menu->addLine(STR_EDIT, [=]() {
        uint8_t idx = button->getIndex();
        editMix(ch, idx);
      });
    if (!reachMixesLimit()) {
      if (this->_copyMode != 0) {
        menu->addLine(STR_PASTE_BEFORE, [=]() {
          uint8_t idx = button->getIndex();
          pasteMixBefore(idx);
        });
        menu->addLine(STR_PASTE_AFTER, [=]() {
          uint8_t idx = button->getIndex();
          pasteMixAfter(idx);
        });
      }
      menu->addLine(STR_INSERT_BEFORE, [=]() {
        uint8_t idx = button->getIndex();
        insertMix(ch, idx);
      });
      menu->addLine(STR_INSERT_AFTER, [=]() {
        uint8_t idx = button->getIndex();
        insertMix(ch, idx + 1);
      });
      menu->addLine(STR_COPY, [=]() {
        this->_copyMode = COPY_MODE;
        this->_copySrc = button;
      });
      menu->addLine(STR_MOVE, [=]() {
        this->_copyMode = MOVE_MODE;
        this->_copySrc = button;
      });
    }
    menu->addLine(STR_DELETE, [=]() {
      uint8_t idx = button->getIndex();
      deleteMix(idx);
    });
    return 0;
  });

  return button;
}

static int mixLineNumber(uint8_t index)
{
  MixData* mix = mixAddress(index);
  if (is_memclear(mix, sizeof(MixData))) return -1;

  int number = 0;
  for (uint8_t i = 0; i <= index && i < MAX_MIXERS; i += 1) {
    MixData* current = mixAddress(i);
    if (is_memclear(current, sizeof(MixData))) break;
    if (current->destCh == mix->destCh) number += 1;
  }
  return number;
}

static uint8_t focusAfterMixDelete(uint8_t deletedIndex)
{
  if (deletedIndex < MAX_MIXERS &&
      !is_memclear(mixAddress(deletedIndex), sizeof(MixData)))
    return deletedIndex;

  for (int i = deletedIndex - 1; i >= 0; i -= 1) {
    if (!is_memclear(mixAddress(i), sizeof(MixData))) return i;
  }

  return UINT8_MAX;
}

void ModelMixesPage::newMix()
{
  Menu* menu = new Menu();
  menu->setTitle(STR_MENU_CHANNELS);

  uint8_t index = 0;
  MixData* line = mixAddress(0);

  // search for unused channels
  for (uint8_t ch = 0; ch < MAX_OUTPUT_CHANNELS; ch++) {
    if (index >= MAX_MIXERS) break;
    bool skip_mix = (ch == 0 && is_memclear(line, sizeof(MixData)));
    if (line->destCh == ch && !skip_mix) {
      while (index < MAX_MIXERS && (line->destCh == ch) && !skip_mix) {
        ++index;
        ++line;
        skip_mix = (ch == 0 && is_memclear(line, sizeof(MixData)));
      }
    } else {
      std::string ch_name(getSourceString(MIXSRC_FIRST_CH + ch));
      menu->addLineBuffered(ch_name.c_str(), [=]() { insertMix(ch, index); });
    }
  }
  menu->updateLines();
}

void ModelMixesPage::editMix(uint8_t channel, uint8_t index)
{
  _copyMode = 0;

  auto edit = new MixEditWindow(channel, index);
  edit->setCloseHandler([=]() {
    MixData* mix = mixAddress(index);
    if (is_memclear(mix, sizeof(MixData))) {
      rebuildFromModel(focusAfterMixDelete(index));
    } else {
      rebuildFromModel(index);
    }
  });
}

void ModelMixesPage::insertMix(uint8_t channel, uint8_t index)
{
  _copyMode = 0;

  ::insertMix(index, channel);
  rebuildFromModel(index);
  editMix(channel, index);
}

void ModelMixesPage::deleteMix(uint8_t index)
{
  auto mix = mixAddress(index);
  if (is_memclear(mix, sizeof(MixData))) return;

  std::string s(getSourceString(MIXSRC_FIRST_CH + mix->destCh));
  s += " - ";
  if (mix->name[0]) {
    s += mix->name;
  } else {
    s += "#";
    s += std::to_string(mixLineNumber(index));
  }

  if (confirmationDialog(STR_DELETE_MIX_LINE, s.c_str())) {
    _copyMode = 0;
    _copySrc = nullptr;

    ::deleteMix(index);
    rebuildFromModel(focusAfterMixDelete(index));
  }
}

void ModelMixesPage::pasteMix(uint8_t dst_idx, uint8_t channel)
{
  if (!_copyMode || !_copySrc) return;
  uint8_t src_idx = _copySrc->getIndex();
  bool move = _copyMode == MOVE_MODE;
  uint8_t deleteIndex = src_idx + (dst_idx <= src_idx ? 1 : 0);

  ::copyMix(src_idx, dst_idx, channel);
  rebuildFromModel(dst_idx);

  if (move) deleteMix(deleteIndex);

  _copyMode = 0;
  _copySrc = nullptr;
}

static int _mixChnFromIndex(uint8_t index)
{
  MixData* mix = mixAddress(index);
  if (is_memclear(mix, sizeof(MixData))) return -1;
  return mix->destCh;
}

void ModelMixesPage::pasteMixBefore(uint8_t dst_idx)
{
  int channel = _mixChnFromIndex(dst_idx);
  if (channel < 0) return;
  pasteMix(dst_idx, channel);
}

void ModelMixesPage::pasteMixAfter(uint8_t dst_idx)
{
  int channel = _mixChnFromIndex(dst_idx);
  if (channel < 0) return;
  pasteMix(dst_idx + 1, channel);
}

void ModelMixesPage::build(Window * window)
{
  bindPageWindow(window);
  _copyMode = 0;
  _copySrc = nullptr;

  window->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_TINY);

  form = new Window(window, rect_t{0, 0, ListLineButton::GRP_W, LV_SIZE_CONTENT});
  form->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_TINY);

  auto box = new Window(window, rect_t{});
  box->padAll(PAD_TINY);
  box->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_SMALL);
  box->padLeft(PAD_MEDIUM);

  box->setStyleFlexCrossPlace(LV_FLEX_ALIGN_CENTER, 0);

  new StaticText(box, rect_t{}, STR_SHOW_MIXER_MONITORS);
  new ToggleSwitch(
      box, rect_t{}, [=]() { return showMonitors; },
      [=](uint8_t val) { enableMonitors(val); });

  auto btn = new TextButton(window, rect_t{}, LV_SYMBOL_PLUS, [=]() {
    newMix();
    return 0;
  });
  btn->setWidth(lv_pct(100));
  btn->focus();

  groups.clear();
  lines.clear();

  bool focusSet = false;
  uint8_t index = 0;
  MixData* line = g_model.mixData;
  for (uint8_t ch = 0; (ch < MAX_OUTPUT_CHANNELS) && (index < MAX_MIXERS); ch++) {

    bool skip_mix = (ch == 0 && is_memclear(line, sizeof(MixData)));
    if (line->destCh == ch && !skip_mix) {

      // one group for the complete mixer channel
      auto group = createGroup(form, MIXSRC_FIRST_CH + ch);
      groups.emplace_back(group);
      while (index < MAX_MIXERS && (line->destCh == ch) && !skip_mix) {
        // one button per input line
        auto btn = createLineButton(group, index);
        if (shouldFocusLine(index, focusSet)) btn->focus();
        ++index;
        ++line;
        skip_mix = (ch == 0 && is_memclear(line, sizeof(MixData)));
      }
    }
  }
}

void ModelMixesPage::enableMonitors(bool enabled)
{
  if (showMonitors == enabled) return;
  showMonitors = enabled;

  for(auto* g : groups) {
    MixGroup* group = (MixGroup*)g;
    if (enabled) {
      group->enableMixerMonitor();
    } else {
      group->disableMixerMonitor();
    }
  }
}
