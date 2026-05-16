/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include <stdio.h>

#include <new>

#include "edgetx.h"
#include "telemetry/battery_monitor.h"
#include "widget.h"

class BatteryMonitorWidget : public NativeWidget
{
  struct BmsData {
    char pack[32];
    char status[24];
    char cellVoltageStr[18];
    char consumedStr[24];
    char deltaStr[16];

    uint8_t remainingPct;
    uint8_t warningLevel;
    uint8_t deltaLevel;
    bool hasVoltage;
    bool hasCellDelta;
    bool hasConsumed;
    bool hasRemaining;
  };

 public:
  BatteryMonitorWidget(const WidgetFactory* factory, Window* parent,
                       const rect_t& rect, WidgetLocation location) :
      NativeWidget(factory, parent, rect, location)
  {
  }

  void createContent(lv_obj_t* parent) override
  {
    (void)parent;

    lv_style_init(&trackStyle);
    lv_style_set_radius(&trackStyle, PILL_RADIUS);

    lv_style_init(&fillStyle);
    lv_style_set_radius(&fillStyle, PILL_RADIUS);

    initRequiredLvObj(
        compactBox,
        [](lv_obj_t* parent) {
          return createFlexBox(parent, LV_FLEX_FLOW_COLUMN);
        },
        [](lv_obj_t* obj) { lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN); });

    initRequiredLvObj(
        compactHeader,
        [&](lv_obj_t* parent) {
          compactBox.with([&](lv_obj_t* obj) { parent = obj; });
          return createFlexBox(parent, LV_FLEX_FLOW_ROW);
        },
        [](lv_obj_t*) {});

    initRequiredLvObj(
        headerPill,
        [](lv_obj_t* parent) {
          auto obj = lv_obj_create(parent);
          if (obj) {
            lv_obj_remove_style_all(obj);
            lv_obj_clear_flag(obj,
                              LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(obj, pillColor(), LV_PART_MAIN);
            lv_obj_set_style_radius(obj, PILL_RADIUS, LV_PART_MAIN);
          }
          return obj;
        },
        [](lv_obj_t*) {});

    initRequiredLvObj(
        packLabel, [](lv_obj_t* parent) { return etx_label_create(parent); },
        [&](lv_obj_t* obj) {
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
          lv_obj_set_style_text_color(obj, lv_color_make(24, 57, 84),
                                      LV_PART_MAIN);
        });

    initRequiredLvObj(
        percentLabel,
        [](lv_obj_t* parent) { return etx_label_create(parent, FONT_L_INDEX); },
        [&](lv_obj_t* obj) {
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
          etx_txt_color(obj, COLOR_BLACK_INDEX);
        });

    initRequiredLvObj(
        cellVoltageLabel,
        [](lv_obj_t* parent) {
          return etx_label_create(parent, FONT_XXS_INDEX);
        },
        [&](lv_obj_t* obj) {
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
          lv_obj_set_style_text_color(obj, lv_color_make(44, 74, 98),
                                      LV_PART_MAIN);
        });

    initRequiredLvObj(
        track,
        [](lv_obj_t* parent) {
          auto obj = lv_obj_create(parent);
          if (obj) {
            lv_obj_remove_style_all(obj);
            lv_obj_clear_flag(obj,
                              LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(obj, trackColor(), LV_PART_MAIN);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(obj, PILL_RADIUS, LV_PART_MAIN);
          }
          return obj;
        },
        [](lv_obj_t*) {});

    initRequiredLvObj(
        fill,
        [](lv_obj_t* parent) {
          auto obj = lv_obj_create(parent);
          if (obj) {
            lv_obj_remove_style_all(obj);
            lv_obj_clear_flag(obj,
                              LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_radius(obj, PILL_RADIUS, LV_PART_MAIN);
          }
          return obj;
        },
        [](lv_obj_t*) {});

    initRequiredLvObj(
        consumedLabel,
        [](lv_obj_t* parent) {
          return etx_label_create(parent, FONT_XXS_INDEX);
        },
        [&](lv_obj_t* obj) {
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
          lv_obj_set_style_text_color(obj, lv_color_make(68, 92, 112),
                                      LV_PART_MAIN);
        });

    initRequiredLvObj(
        deltaLabel,
        [](lv_obj_t* parent) {
          return etx_label_create(parent, FONT_XXS_INDEX);
        },
        [&](lv_obj_t* obj) {
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
          lv_obj_set_style_text_color(obj, lv_color_make(68, 92, 112),
                                      LV_PART_MAIN);
        });

    initRequiredLvObj(
        statusPill,
        [](lv_obj_t* parent) {
          auto obj = lv_obj_create(parent);
          if (obj) {
            lv_obj_remove_style_all(obj);
            lv_obj_clear_flag(obj,
                              LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(obj, pillColor(), LV_PART_MAIN);
            lv_obj_set_style_radius(obj, PILL_RADIUS, LV_PART_MAIN);
          }
          return obj;
        },
        [](lv_obj_t*) {});

    initRequiredLvObj(
        statusLabel,
        [](lv_obj_t* parent) {
          return etx_label_create(parent, FONT_XXS_INDEX);
        },
        [&](lv_obj_t* obj) {
          lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
          lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
          lv_obj_set_style_text_color(obj, lv_color_make(24, 57, 84),
                                      LV_PART_MAIN);
        });
  }

  static const WidgetOption options[];

 protected:
  lv_style_t trackStyle;
  lv_style_t fillStyle;
  RequiredLvObj headerPill;
  RequiredLvObj packLabel;
  RequiredLvObj percentLabel;
  RequiredLvObj cellVoltageLabel;
  RequiredLvObj track;
  RequiredLvObj fill;
  RequiredLvObj consumedLabel;
  RequiredLvObj deltaLabel;
  RequiredLvObj statusPill;
  RequiredLvObj statusLabel;
  RequiredLvObj compactBox;
  RequiredLvObj compactHeader;

  uint8_t lastRemainingPct = 255;
  uint8_t lastWarningLevel = 255;
  int32_t lastDeltaCv = -1;

  // true = percent is in main row (not inside bar)
  bool percentInMainRow = true;

  static const char* chemAbbrev(BatteryType t)
  {
    switch (t) {
      case BATTERY_TYPE_LIPO:
        return "LiPo";
      case BATTERY_TYPE_LIION:
        return "Li-Ion";
      case BATTERY_TYPE_LIFE:
        return "LiFe";
      case BATTERY_TYPE_NIMH:
        return "NiMH";
      case BATTERY_TYPE_PB:
        return "Pb";
      default:
        return "?";
    }
  }

  static void formatCv(char* buf, size_t len, uint16_t cv, const char* suffix)
  {
    snprintf(buf, len, "%d.%02u%s", cv / 100, (unsigned)(cv % 100), suffix);
  }

  void setVis(lv_obj_t* obj, bool vis) { setObjVisible(obj, vis); }

  void setObjRect(lv_obj_t* obj, coord_t x, coord_t y, coord_t w, coord_t h)
  {
    if (!obj) return;
    lv_obj_set_pos(obj, x, y);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    lv_obj_set_size(obj, w, h);
  }

  void collectData(BmsData& d)
  {
    d.remainingPct = 0;
    d.warningLevel = 0;
    d.deltaLevel = 0;
    d.hasVoltage = false;
    d.hasCellDelta = false;
    d.hasConsumed = false;
    d.hasRemaining = false;
    d.cellVoltageStr[0] = '\0';
    d.consumedStr[0] = '\0';
    d.deltaStr[0] = '\0';

    auto& config = g_model.batteryMonitors[0];
    auto& runtime = flightBatteryRuntimeState[0];
    auto state = flightBatterySessionState(0);

    if (!config.enabled) {
      snprintf(d.pack, sizeof(d.pack), "Monitor off");
      snprintf(d.status, sizeof(d.status), "Disabled");
      d.remainingPct = 0;
      return;
    }

    uint8_t cells = config.cellCount;
    int16_t cap = config.capacity;
    BatteryType chem = (BatteryType)config.batteryType;

    if (config.selectedPackSlot > 0) {
      uint8_t slot = config.selectedPackSlot - 1;
      if (slot < MAX_BATTERY_PACKS && g_eeGeneral.batteryPacks[slot].active) {
        cells = g_eeGeneral.batteryPacks[slot].cellCount;
        cap = g_eeGeneral.batteryPacks[slot].capacity;
        chem = (BatteryType)g_eeGeneral.batteryPacks[slot].batteryType;
      }
    }

    if (cells > 0 && cap > 0 && chem <= BATTERY_TYPE_PB) {
      snprintf(d.pack, sizeof(d.pack), "%s %dS %d", chemAbbrev(chem), cells,
               cap);
    } else if (cells > 0) {
      snprintf(d.pack, sizeof(d.pack), "%s %dS", chemAbbrev(chem), cells);
    } else {
      snprintf(d.pack, sizeof(d.pack), "Battery 1");
    }

    switch (state) {
      case FlightBatterySessionState::Unknown:
      case FlightBatterySessionState::WaitingForVoltage:
        snprintf(d.status, sizeof(d.status), "Waiting...");
        break;
      case FlightBatterySessionState::NoBatteryObserved:
        snprintf(d.status, sizeof(d.status), "No battery");
        d.remainingPct = 0;
        break;
      case FlightBatterySessionState::NeedsConfirmation:
        snprintf(d.status, sizeof(d.status), "Select battery");
        break;
      case FlightBatterySessionState::VoltageMismatch:
        snprintf(d.status, sizeof(d.status), "Voltage mismatch");
        d.warningLevel = 2;
        break;
      case FlightBatterySessionState::NeedsConfiguration:
        snprintf(d.status, sizeof(d.status), "Needs config");
        break;
      case FlightBatterySessionState::Confirmed:
        snprintf(d.status, sizeof(d.status), "Confirmed");
        break;
      case FlightBatterySessionState::ConfirmedWaitingForVoltage:
        snprintf(d.status, sizeof(d.status), "Telemetry lost");
        break;
    }

    int srcIdx = config.sourceIndex - 1;

    // Read voltage
    if (srcIdx >= 0 && srcIdx < MAX_TELEMETRY_SENSORS) {
      auto& sensor = g_model.telemetrySensors[srcIdx];
      auto& item = telemetryItems[srcIdx];
      if (sensor.isAvailable() && item.isAvailable() && !item.isOld() &&
          item.value > 0) {
        d.hasVoltage = true;

        if (sensor.unit == UNIT_CELLS) {
          auto& cellsData = item.cells;
          if (cellsData.count > 0) {
            uint16_t lowest = UINT16_MAX;
            uint16_t highest = 0;
            uint32_t sum = 0;
            bool allValid = true;
            int validCount = 0;
            for (int i = 0; i < cellsData.count && i < MAX_CELLS; i++) {
              if (!cellsData.values[i].state) {
                allValid = false;
                break;
              }
              uint16_t v = cellsData.values[i].value;
              sum += v;
              validCount++;
              if (v < lowest) lowest = v;
              if (v > highest) highest = v;
            }
            if (allValid && validCount > 0) {
              uint16_t avgCv = (uint16_t)(sum / validCount);
              formatCv(d.cellVoltageStr, sizeof(d.cellVoltageStr), avgCv,
                       "/cell");

              if (validCount > 1) {
                d.hasCellDelta = true;
                uint16_t delta = highest - lowest;
                char tmp[8];
                formatCv(tmp, sizeof(tmp), delta, "V");
                snprintf(d.deltaStr, sizeof(d.deltaStr), "+/- %s", tmp);
                if (delta <= 3)
                  d.deltaLevel = 0;
                else if (delta <= 6)
                  d.deltaLevel = 1;
                else
                  d.deltaLevel = 2;
              }
            } else {
              int32_t cv = convertTelemetryValue(item.value, sensor.unit,
                                                 sensor.prec, UNIT_VOLTS, 2);
              if (cv > 0 && cells > 0) {
                uint16_t perCell = (uint16_t)(cv / cells);
                formatCv(d.cellVoltageStr, sizeof(d.cellVoltageStr), perCell,
                         "/cell");
              }
            }
          }
        } else if (sensor.unit == UNIT_VOLTS) {
          int32_t cv = convertTelemetryValue(item.value, sensor.unit,
                                             sensor.prec, UNIT_VOLTS, 2);
          if (cv > 0 && cells > 0) {
            uint16_t perCell = (uint16_t)(cv / cells);
            formatCv(d.cellVoltageStr, sizeof(d.cellVoltageStr), perCell,
                     "/cell");
          }
        }
      }
    }

    // Read capacity
    if (cap > 0 && config.currentIndex > 0) {
      int curIdx = config.currentIndex - 1;
      if (curIdx >= 0 && curIdx < MAX_TELEMETRY_SENSORS) {
        auto& curSensor = g_model.telemetrySensors[curIdx];
        auto& curItem = telemetryItems[curIdx];
        if (curSensor.isAvailable() && curItem.isAvailable() &&
            !curItem.isOld() && curSensor.unit == UNIT_MAH) {
          d.hasConsumed = true;
          int32_t raw = curItem.value;
          int32_t session = flightBatterySessionConsumedFromRaw(runtime, raw);

          if (session > 0)
            snprintf(d.consumedStr, sizeof(d.consumedStr), "%ld mAh",
                     (long)session);
          else
            snprintf(d.consumedStr, sizeof(d.consumedStr), "-- mAh");

          int pct = 100;
          if (cap > 0) {
            pct = 100 - (int)((int64_t)session * 100 / cap);
            if (pct < 0) pct = 0;
          }
          d.remainingPct = (uint8_t)pct;
          d.hasRemaining = true;

          int consumed = 100 - d.remainingPct;
          if (consumed >= 80)
            d.warningLevel = 2;
          else if (consumed >= 65)
            d.warningLevel = 1;
        }
      }
    }

    // Voltage fallback for remaining %
    if (!d.hasConsumed && d.hasVoltage && cells > 0) {
      int32_t perCell = 0;
      auto& sensor = g_model.telemetrySensors[srcIdx];
      if (sensor.unit == UNIT_CELLS) {
        auto& cellsData = telemetryItems[srcIdx].cells;
        if (cellsData.count > 0) {
          uint16_t lowest = UINT16_MAX;
          bool allValid = true;
          for (int i = 0; i < cellsData.count && i < MAX_CELLS; i++) {
            if (!cellsData.values[i].state) {
              allValid = false;
              break;
            }
            if (cellsData.values[i].value < lowest)
              lowest = cellsData.values[i].value;
          }
          if (allValid) perCell = lowest;
        }
      }
      if (perCell == 0) {
        int32_t cv =
            convertTelemetryValue(telemetryItems[srcIdx].value, sensor.unit,
                                  sensor.prec, UNIT_VOLTS, 2);
        if (cv > 0) perCell = cv / cells;
      }

      if (perCell > 0) {
        uint16_t minCv = batteryTypeMatchMinPerCellCv(chem);
        uint16_t maxCv = batteryTypeMatchMaxPerCellCv(chem);
        if (maxCv > minCv) {
          int pct = (int)(perCell - (int32_t)minCv) * 100 / (maxCv - minCv);
          if (pct < 0) pct = 0;
          if (pct > 100) pct = 100;
          d.remainingPct = (uint8_t)pct;
          d.hasRemaining = true;

          if (d.remainingPct <= 20)
            d.warningLevel = 2;
          else if (d.remainingPct <= 35)
            d.warningLevel = 1;
        }
      }
    }

    if (!d.hasCellDelta && d.hasVoltage && cells > 0) {
      snprintf(d.deltaStr, sizeof(d.deltaStr), "+/- --");
    }
  }

  uint32_t contentRefreshKey() override
  {
    WidgetRefreshKey key;
    key.add((uint32_t)flightBatterySessionState(0));

    auto& config = g_model.batteryMonitors[0];
    if (config.currentIndex > 0) {
      int idx = config.currentIndex - 1;
      if (idx < MAX_TELEMETRY_SENSORS) {
        key.add(telemetryItems[idx].value)
            .add(telemetryItems[idx].isAvailable())
            .add(telemetryItems[idx].isOld());
      }
    }
    if (config.sourceIndex > 0) {
      int idx = config.sourceIndex - 1;
      if (idx < MAX_TELEMETRY_SENSORS) {
        key.add(telemetryItems[idx].value)
            .add(telemetryItems[idx].isAvailable())
            .add(telemetryItems[idx].isOld());
      }
    }
    return key.value();
  }

  void layoutContent(const rect_t& content) override
  {
    const bool compact = isCompactTopBarWidget();
    const coord_t h = compact ? height() : content.h;
    const coord_t w = compact ? width() : content.w;
    const coord_t ox = compact ? 0 : content.x;
    const coord_t oy = compact ? 0 : content.y;

    auto moveToRoot = [&](auto& handle) {
      handle.with([&](lv_obj_t* obj) {
        withLive(
            [&](LiveWindow& live) { lv_obj_set_parent(obj, live.lvobj()); });
      });
    };

    headerPill.with([&](lv_obj_t* obj) { setVis(obj, !compact); });
    statusPill.with([&](lv_obj_t* obj) { setVis(obj, !compact); });

    if (compact) {
      // Topbar compact: bar + percent inside
      percentInMainRow = false;
      compactBox.with([&](lv_obj_t* obj) { setVis(obj, false); });
      moveToRoot(track);
      moveToRoot(fill);
      moveToRoot(percentLabel);
      headerPill.with([&](lv_obj_t* obj) { setVis(obj, false); });
      packLabel.with([&](lv_obj_t* obj) { setVis(obj, false); });
      cellVoltageLabel.with([&](lv_obj_t* obj) { setVis(obj, false); });
      consumedLabel.with([&](lv_obj_t* obj) { setVis(obj, false); });
      deltaLabel.with([&](lv_obj_t* obj) { setVis(obj, false); });
      statusPill.with([&](lv_obj_t* obj) { setVis(obj, false); });
      statusLabel.with([&](lv_obj_t* obj) { setVis(obj, false); });

      coord_t barH = h / 2;
      if (barH < 6) barH = 6;
      if (barH > 14) barH = 14;
      coord_t barY = (h - barH) / 2;
      track.with([&](lv_obj_t* obj) { setObjRect(obj, 0, barY, w, barH); });
      fill.with([&](lv_obj_t* obj) { setObjRect(obj, 0, barY, w, barH); });

      percentLabel.with([&](lv_obj_t* obj) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        FontIndex font = FONT_BOLD_INDEX;
        if (barH >= 12) font = FONT_STD_INDEX;
        etx_font(obj, font);
        coord_t fh = getFontHeight(LcdFlags(font) << 8u);
        lv_obj_set_pos(obj, 2, barY + (barH - fh) / 2);
        lv_obj_set_size(obj, w - 4, fh);
      });
      lastRemainingPct = 255;
      return;
    }

    // -- Non-compact layout --

    percentInMainRow = true;
    const coord_t cx = ox;
    const coord_t cy = oy;
    const coord_t cw = w;
    const coord_t ch = h;

    if (usesCardChrome() && h < 58) {
      headerPill.with([&](lv_obj_t* obj) { setVis(obj, false); });
      statusPill.with([&](lv_obj_t* obj) { setVis(obj, false); });
      statusLabel.with([&](lv_obj_t* obj) { setVis(obj, false); });
      packLabel.with([&](lv_obj_t* obj) { setVis(obj, true); });
      percentLabel.with([&](lv_obj_t* obj) { setVis(obj, true); });
      const bool showShortMetrics = cw >= 110 && h >= 40;
      const bool showShortVoltage = showShortMetrics && cw >= 170;
      cellVoltageLabel.with(
          [&](lv_obj_t* obj) { setVis(obj, showShortVoltage); });
      consumedLabel.with([&](lv_obj_t* obj) { setVis(obj, showShortMetrics); });
      deltaLabel.with([&](lv_obj_t* obj) { setVis(obj, showShortMetrics); });

      compactBox.with([&](lv_obj_t* obj) { setVis(obj, false); });
      moveToRoot(packLabel);
      moveToRoot(percentLabel);
      moveToRoot(track);
      moveToRoot(cellVoltageLabel);
      moveToRoot(consumedLabel);
      moveToRoot(deltaLabel);

      FontIndex smallFont = FONT_XXS_INDEX;
      coord_t headerH = getFontHeight(LcdFlags(smallFont) << 8u);
      coord_t metricH = showShortMetrics ? headerH : 0;
      coord_t gap = cardGap(content);
      coord_t metricY = cy + ch - metricH;
      coord_t barY = cy + headerH + gap;
      coord_t barBottom = showShortMetrics ? metricY - gap : cy + ch;
      coord_t barH = barBottom > barY ? barBottom - barY : cardBarHeight(content);
      if (barH < cardBarHeight(content)) barH = cardBarHeight(content);

      coord_t pctW = 54;
      if (pctW > cw / 2) pctW = cw / 2;
      packLabel.with([&](lv_obj_t* obj) {
        etx_font(obj, smallFont);
        lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        setObjRect(obj, cx, cy, cw - pctW, headerH);
      });
      percentLabel.with([&](lv_obj_t* obj) {
        etx_font(obj, smallFont);
        lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        setObjRect(obj, cx + cw - pctW, cy, pctW, headerH);
      });
      track.with([&](lv_obj_t* obj) {
        setObjRect(obj, cx, barY, cw, barH);
      });
      fill.with([&](lv_obj_t* obj) {
        track.with([&](lv_obj_t* t) { lv_obj_set_parent(obj, t); });
        lv_obj_set_pos(obj, 0, 0);
        lv_obj_set_size(obj, cw, barH);
      });
      if (showShortMetrics) {
        if (showShortVoltage) {
          coord_t usedW = cw / 3;
          coord_t deltaW = cw / 3;
          coord_t voltageW = cw - usedW - deltaW;
          cellVoltageLabel.with([&](lv_obj_t* obj) {
            etx_font(obj, smallFont);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            setObjRect(obj, cx, metricY, voltageW, metricH);
          });
          consumedLabel.with([&](lv_obj_t* obj) {
            etx_font(obj, smallFont);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER,
                                        LV_PART_MAIN);
            setObjRect(obj, cx + voltageW, metricY, usedW, metricH);
          });
          deltaLabel.with([&](lv_obj_t* obj) {
            etx_font(obj, smallFont);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT,
                                        LV_PART_MAIN);
            setObjRect(obj, cx + voltageW + usedW, metricY, deltaW, metricH);
          });
        } else {
          coord_t halfW = cw / 2;
          consumedLabel.with([&](lv_obj_t* obj) {
            etx_font(obj, smallFont);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            setObjRect(obj, cx, metricY, halfW, metricH);
          });
          deltaLabel.with([&](lv_obj_t* obj) {
            etx_font(obj, smallFont);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT,
                                        LV_PART_MAIN);
            setObjRect(obj, cx + halfW, metricY, cw - halfW, metricH);
          });
        }
      }
      lastRemainingPct = 255;
      return;
    }

    compactBox.with([&](lv_obj_t* obj) { setVis(obj, false); });
    moveToRoot(packLabel);
    moveToRoot(percentLabel);
    moveToRoot(track);
    moveToRoot(fill);

    // Determine what to show. The fuel gauge is the primary object; labels
    // only stay if they help the pilot make a quick pack decision.
    bool showPack = (h >= 38);
    bool showVoltage = (h >= 76);
    bool showMetrics = (h >= 76);
    bool showThreeMetrics = showMetrics && showVoltage && (cw >= 150);
    bool showSecondaryVoltage =
        showVoltage && !showThreeMetrics && !showMetrics;
    bool showTopStatus = (h >= 126 && cw >= 168);

    headerPill.with([&](lv_obj_t* obj) { setVis(obj, showPack); });
    packLabel.with([&](lv_obj_t* obj) { setVis(obj, showPack); });
    cellVoltageLabel.with([&](lv_obj_t* obj) {
      setVis(obj, showThreeMetrics || showSecondaryVoltage);
    });
    consumedLabel.with([&](lv_obj_t* obj) { setVis(obj, showMetrics); });
    deltaLabel.with([&](lv_obj_t* obj) { setVis(obj, showMetrics); });
    statusPill.with([&](lv_obj_t* obj) { setVis(obj, showTopStatus); });
    statusLabel.with([&](lv_obj_t* obj) { setVis(obj, showTopStatus); });
    percentLabel.with([&](lv_obj_t* obj) {
      lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    });

    coord_t headerH = showPack ? (h < 58 ? 14 : 18) : 0;
    coord_t headerGap = showPack ? (h < 58 ? 3 : 6) : 0;
    coord_t metricH = showMetrics ? 16 : 0;
    coord_t metricGap = showMetrics ? (h >= 108 ? 8 : 6) : 0;
    coord_t secondaryH = showSecondaryVoltage ? 15 : 0;
    coord_t secondaryGap = secondaryH ? 3 : 0;

    coord_t fuelTop = cy + headerH + headerGap;
    coord_t metricsY = cy + ch - metricH;
    coord_t secondaryY = metricsY - metricGap - secondaryH;
    coord_t fuelBottom = secondaryY - secondaryGap;
    if (showThreeMetrics) fuelBottom = metricsY - metricGap;
    if (!showMetrics) fuelBottom = cy + ch;

    coord_t fuelSpace = fuelBottom - fuelTop;
    coord_t minBarH = h < 58 ? 14 : 24;
    if (fuelSpace < minBarH) fuelSpace = minBarH;
    coord_t barH = fuelSpace / 2;
    if (barH < minBarH) barH = minBarH;
    if (barH > (h < 58 ? 24 : 54)) barH = (h < 58 ? 24 : 54);
    if (barH > fuelSpace) barH = fuelSpace;
    coord_t barY = fuelTop + (fuelSpace - barH) / 2;

    // Pack identity chip
    if (showPack) {
      coord_t statusW = showTopStatus ? 74 : 0;
      coord_t chipW = showTopStatus ? (cw - statusW - 4) : cw;
      if (chipW > 132) chipW = 132;
      if (chipW < 76) chipW = cw;
      coord_t chipX = showTopStatus ? cx : (cx + (cw - chipW) / 2);
      headerPill.with(
          [&](lv_obj_t* obj) { setObjRect(obj, chipX, cy, chipW, headerH); });
      packLabel.with([&](lv_obj_t* obj) {
        lv_obj_set_pos(obj, chipX + 5, cy + 2);
        lv_obj_set_size(obj, chipW - 10, headerH - 4);
        etx_font(obj, FONT_XXS_INDEX);
      });

      if (showTopStatus) {
        coord_t statusX = cx + cw - statusW;
        statusPill.with([&](lv_obj_t* obj) {
          setObjRect(obj, statusX, cy, statusW, headerH);
        });
        statusLabel.with([&](lv_obj_t* obj) {
          lv_obj_set_pos(obj, statusX + 5, cy + 2);
          lv_obj_set_size(obj, statusW - 10, headerH - 4);
          etx_font(obj, FONT_XXS_INDEX);
        });
      }
    }

    // Hero fuel gauge: the bar conveys remaining energy, percent annotates it.
    FontIndex pctFont = (barH >= 36)
                            ? FONT_L_INDEX
                            : (barH >= 18 ? FONT_BOLD_INDEX : FONT_STD_INDEX);
    coord_t pctFh = getFontHeight(LcdFlags(pctFont) << 8u);
    coord_t pctY = barY + (barH - pctFh) / 2;

    percentLabel.with([&](lv_obj_t* obj) {
      etx_font(obj, pctFont);
      lv_obj_set_pos(obj, cx, pctY);
      lv_obj_set_size(obj, cw, pctFh);
      lv_obj_move_foreground(obj);
    });

    // Bar
    track.with([&](lv_obj_t* obj) { setObjRect(obj, cx, barY, cw, barH); });
    fill.with([&](lv_obj_t* obj) { setObjRect(obj, cx, barY, cw, barH); });
    percentLabel.with([&](lv_obj_t* obj) { lv_obj_move_foreground(obj); });

    if (showSecondaryVoltage) {
      cellVoltageLabel.with([&](lv_obj_t* obj) {
        lv_obj_set_pos(obj, cx, secondaryY);
        lv_obj_set_size(obj, cw, secondaryH);
        etx_font(obj, FONT_XS_INDEX);
      });
    }

    // Metrics row
    if (showMetrics) {
      if (showThreeMetrics) {
        coord_t deltaW = (cw >= 240) ? cw / 3 : 50;
        coord_t usedW = (cw >= 240) ? cw / 3 : 64;
        if (deltaW + usedW + 48 > cw) {
          deltaW = cw / 4;
          usedW = cw / 3;
        }
        coord_t voltageW = cw - usedW - deltaW;
        cellVoltageLabel.with([&](lv_obj_t* obj) {
          lv_obj_set_pos(obj, cx, metricsY);
          lv_obj_set_size(obj, voltageW, metricH);
          lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
          etx_font(obj, FONT_XXS_INDEX);
        });
        consumedLabel.with([&](lv_obj_t* obj) {
          lv_obj_set_pos(obj, cx + voltageW, metricsY);
          lv_obj_set_size(obj, usedW, metricH);
          lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
          etx_font(obj, FONT_XXS_INDEX);
        });
        deltaLabel.with([&](lv_obj_t* obj) {
          lv_obj_set_pos(obj, cx + voltageW + usedW, metricsY);
          lv_obj_set_size(obj, deltaW, metricH);
          etx_font(obj, FONT_XXS_INDEX);
        });
      } else {
        coord_t mHalf = cw / 2;
        consumedLabel.with([&](lv_obj_t* obj) {
          lv_obj_set_pos(obj, cx, metricsY);
          lv_obj_set_size(obj, mHalf, metricH);
          lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
          etx_font(obj, FONT_XXS_INDEX);
        });
        deltaLabel.with([&](lv_obj_t* obj) {
          lv_obj_set_pos(obj, cx + mHalf, metricsY);
          lv_obj_set_size(obj, cw - mHalf, metricH);
          etx_font(obj, FONT_XXS_INDEX);
        });
      }
    }

    lastRemainingPct = 255;
  }

  void refreshContent() override
  {
    BmsData d;
    collectData(d);

    packLabel.with([&](lv_obj_t* obj) { lv_label_set_text(obj, d.pack); });
    statusLabel.with([&](lv_obj_t* obj) { lv_label_set_text(obj, d.status); });
    cellVoltageLabel.with([&](lv_obj_t* obj) {
      if (d.hasVoltage)
        lv_label_set_text(obj, d.cellVoltageStr);
      else
        lv_label_set_text(obj, "--/cell");
    });
    consumedLabel.with(
        [&](lv_obj_t* obj) { lv_label_set_text(obj, d.consumedStr); });

    // Delta text + color
    deltaLabel.with([&](lv_obj_t* obj) {
      lv_label_set_text(obj, d.deltaStr);
      if (d.deltaLevel == 2)
        etx_txt_color(obj, COLOR_RED_INDEX);
      else if (d.deltaLevel == 1)
        etx_txt_color(obj, COLOR_THEME_WARNING_INDEX);
      else
        lv_obj_set_style_text_color(obj, lv_color_make(68, 92, 112),
                                    LV_PART_MAIN);
    });

    // Percent text (always update in case position changed)
    char pctBuf[5];
    if (d.hasRemaining)
      snprintf(pctBuf, sizeof(pctBuf), "%u%%", d.remainingPct);
    else
      snprintf(pctBuf, sizeof(pctBuf), "--%%");
    percentLabel.with([&](lv_obj_t* obj) {
      lv_label_set_text(obj, pctBuf);
      if (usesCardChrome())
        lv_obj_set_style_text_color(obj, primaryTextColor(), LV_PART_MAIN);
      else
        etx_txt_color(obj, COLOR_BLACK_INDEX);
    });

    statusPill.with([&](lv_obj_t* obj) {
      if (d.warningLevel == 2)
        lv_obj_set_style_bg_color(obj, lv_color_make(255, 229, 229),
                                  LV_PART_MAIN);
      else if (d.warningLevel == 1)
        lv_obj_set_style_bg_color(obj, lv_color_make(255, 238, 196),
                                  LV_PART_MAIN);
      else
        lv_obj_set_style_bg_color(obj, lv_color_make(226, 238, 247),
                                  LV_PART_MAIN);
    });
    statusLabel.with([&](lv_obj_t* obj) {
      if (d.warningLevel == 2)
        etx_txt_color(obj, COLOR_RED_INDEX);
      else if (d.warningLevel == 1)
        etx_txt_color(obj, COLOR_THEME_WARNING_INDEX);
      else
        lv_obj_set_style_text_color(obj, lv_color_make(24, 57, 84),
                                    LV_PART_MAIN);
    });

    // Update bar only when data changes
    if (d.remainingPct != lastRemainingPct ||
        d.warningLevel != lastWarningLevel) {
      lastRemainingPct = d.remainingPct;
      lastWarningLevel = d.warningLevel;

      coord_t tw = 1;
      track.with([&](lv_obj_t* obj) { tw = lv_obj_get_width(obj); });
      coord_t barH = 1;
      track.with([&](lv_obj_t* obj) { barH = lv_obj_get_height(obj); });

      coord_t fillW = d.hasRemaining
                          ? (coord_t)((int)tw * d.remainingPct / 100)
                          : 0;

      LcdColorIndex fc;
      if (d.warningLevel == 2)
        fc = COLOR_RED_INDEX;
      else if (d.warningLevel == 1)
        fc = COLOR_THEME_WARNING_INDEX;
      else
        fc = COLOR_GREEN_INDEX;

      fill.with([&](lv_obj_t* obj) {
        lv_obj_set_size(obj, fillW, barH);
        etx_solid_bg(obj, fc);
      });
    }
  }
};

const WidgetOption BatteryMonitorWidget::options[] = {
    {nullptr, WidgetOption::Bool}};

BaseWidgetFactory<BatteryMonitorWidget> batteryMonitorWidget(
    "Battery Monitor", BatteryMonitorWidget::options, "Battery Monitor");
