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

#include "curve.h"

#include <new>

#include "bitmaps.h"
#include "edgetx.h"
#include "static.h"
#include "strhelpers.h"

//-----------------------------------------------------------------------------

CurveRenderer::CurveRenderer(Window& parent, const rect_t& rect,
                             std::function<int(int)> function)
{
  parent.withLive(
      [&](Window::LiveWindow& live) { init(live, rect, std::move(function)); });
}

bool CurveRenderer::init(Window::LiveWindow& parent, const rect_t& rect,
                         std::function<int(int)> function)
{
  auto parentObj = parent.lvobj();
  valueFunc = std::move(function);
  dx = rect.x;
  dy = rect.y;
  dw = rect.w;
  dh = rect.h;

  lv_obj_t* bgBox = lv_line_create(parentObj);
  if (!bgBox) return false;
  etx_obj_add_style(bgBox, styles->graph_border, LV_PART_MAIN);

  lv_obj_t* axis1 = lv_line_create(parentObj);
  if (!axis1) return false;
  etx_obj_add_style(axis1, styles->graph_border, LV_PART_MAIN);
  lv_obj_t* axis2 = lv_line_create(parentObj);
  if (!axis2) return false;
  etx_obj_add_style(axis2, styles->graph_border, LV_PART_MAIN);

  lv_obj_t* extra1 = lv_line_create(parentObj);
  if (!extra1) return false;
  etx_obj_add_style(extra1, styles->graph_dashed, LV_PART_MAIN);
  lv_obj_t* extra2 = lv_line_create(parentObj);
  if (!extra2) return false;
  etx_obj_add_style(extra2, styles->graph_dashed, LV_PART_MAIN);
  lv_obj_t* extra3 = lv_line_create(parentObj);
  if (!extra3) return false;
  etx_obj_add_style(extra3, styles->graph_dashed, LV_PART_MAIN);
  lv_obj_t* extra4 = lv_line_create(parentObj);
  if (!extra4) return false;
  etx_obj_add_style(extra4, styles->graph_dashed, LV_PART_MAIN);

  // Outer box
  bgPoints[0] = {dx, dy};
  bgPoints[1] = {(lv_coord_t)(dx + dw - 1), dy};
  bgPoints[2] = {(lv_coord_t)(dx + dw - 1), (lv_coord_t)(dy + dh - 1)};
  bgPoints[3] = {dx, (lv_coord_t)(dy + dh - 1)};
  bgPoints[4] = {dx, dy};

  lv_line_set_points(bgBox, bgPoints, 5);

  // Axis lines
  bgPoints[5] = {(lv_coord_t)(dx + dw / 2), dy};
  bgPoints[6] = {(lv_coord_t)(dx + dw / 2), (lv_coord_t)(dy + dh - 1)};
  bgPoints[7] = {dx, (lv_coord_t)(dy + dh / 2)};
  bgPoints[8] = {(lv_coord_t)(dx + dw - 1), (lv_coord_t)(dy + dh / 2)};

  lv_line_set_points(axis1, &bgPoints[5], 2);
  lv_line_set_points(axis2, &bgPoints[7], 2);

  // Extra lines
  bgPoints[9] = {(lv_coord_t)(dx + dw / 4), dy};
  bgPoints[10] = {(lv_coord_t)(dx + dw / 4), (lv_coord_t)(dy + dh - 1)};
  bgPoints[11] = {(lv_coord_t)(dx + dw * 3 / 4), dy};
  bgPoints[12] = {(lv_coord_t)(dx + dw * 3 / 4), (lv_coord_t)(dy + dh - 1)};
  bgPoints[13] = {dx, (lv_coord_t)(dy + dh / 4)};
  bgPoints[14] = {(lv_coord_t)(dx + dw - 1), (lv_coord_t)(dy + dh / 4)};
  bgPoints[15] = {dx, (lv_coord_t)(dy + dh * 3 / 4)};
  bgPoints[16] = {(lv_coord_t)(dx + dw - 1), (lv_coord_t)(dy + dh * 3 / 4)};

  lv_line_set_points(extra1, &bgPoints[9], 2);
  lv_line_set_points(extra2, &bgPoints[11], 2);
  lv_line_set_points(extra3, &bgPoints[13], 2);
  lv_line_set_points(extra4, &bgPoints[15], 2);

  // Curve points
  lnPoints = new (std::nothrow) lv_point_t[dw];
  if (!lnPoints) return false;

  ptLine = lv_line_create(parentObj);
  if (!ptLine) return false;
  etx_obj_add_style(ptLine, styles->graph_line, LV_PART_MAIN);

  update();
  return true;
}

CurveRenderer::~CurveRenderer() { delete[] lnPoints; }

void CurveRenderer::update()
{
  if (!ready()) return;

  for (lv_coord_t x = 0; x < dw; x += 1) {
    lv_coord_t y =
        getPointY(valueFunc(divRoundClosest((x - dw / 2) * RESX, dw / 2)));
    lnPoints[x] = {(lv_coord_t)(x + dx), y};
  }

  lv_line_set_points(ptLine, lnPoints, dw);
}

coord_t CurveRenderer::getPointY(int y) const
{
  return dy +
         limit<coord_t>(0, dh / 2 - divRoundClosest(y * dh / 2, RESX), dh - 1);
}

//-----------------------------------------------------------------------------

Curve::Curve(Window* parent, const rect_t& rect,
             std::function<int(int)> function, std::function<int()> position) :
    Window(parent, rect),
    valueFunc(std::move(function)),
    positionFunc(std::move(position))
{
  setWindowFlag(NO_FOCUS | NO_CLICK);

  if (!withLive([&](LiveWindow& live) {
        auto obj = live.lvobj();
        auto renderRect =
            rect_t{(positionFunc ? POS_PT_SZ / 2 : PAD_BORDER),
                   (positionFunc ? POS_PT_SZ / 2 : PAD_BORDER),
                   rect.w - (positionFunc ? POS_PT_SZ & 0xFE : PAD_BORDER * 2),
                   rect.h - (positionFunc ? POS_PT_SZ & 0xFE : PAD_BORDER * 2)};
        if (!base.init(live, renderRect, valueFunc)) {
          failClosed();
          return false;
        }

        etx_solid_bg(obj, COLOR_THEME_PRIMARY2_INDEX);

        // Adjust border - if drawing points leave more space to prevent
        // clipping of end points.
        if (positionFunc) {
          dx = POS_PT_SZ / 2;
          dy = POS_PT_SZ / 2;
        } else {
          dx = PAD_BORDER;
          dy = PAD_BORDER;
        }
        dw = rect.w - dx * 2;
        dh = rect.h - dy * 2;

        if (positionFunc) {
          posVLine = lv_line_create(obj);
          if (!requireLvObj(posVLine)) return false;
          etx_obj_add_style(posVLine, styles->graph_position_line,
                            LV_PART_MAIN);
          posHLine = lv_line_create(obj);
          if (!requireLvObj(posHLine)) return false;
          etx_obj_add_style(posHLine, styles->graph_position_line,
                            LV_PART_MAIN);

          positionValue = Window::makeLive<StaticText>(
              this, rect_t{POS_LBL_X, POS_LBL_Y, LV_SIZE_CONTENT, POS_LBL_H},
              "", COLOR_THEME_PRIMARY1_INDEX, FONT(XS));
          if (!positionValue) {
            failClosed();
            return false;
          }
          positionValue->padLeft(PAD_TINY);
          positionValue->padRight(PAD_TINY);
          positionValue->withLive([](Window::LiveWindow& livePosition) {
            etx_solid_bg(livePosition.lvobj(), COLOR_THEME_ACTIVE_INDEX);
          });

          posPoint = lv_obj_create(obj);
          if (!requireLvObj(posPoint)) return false;
          etx_solid_bg(posPoint, COLOR_THEME_PRIMARY2_INDEX);
          etx_obj_add_style(posPoint, styles->circle, LV_PART_MAIN);
          etx_obj_add_style(posPoint, styles->border, LV_PART_MAIN);
          etx_obj_add_style(posPoint,
                            styles->border_color[COLOR_THEME_ACTIVE_INDEX],
                            LV_PART_MAIN);
          lv_obj_set_size(posPoint, POS_PT_SZ, POS_PT_SZ);

          updatePosition();
        }

        return true;
      })) {
    return;
  }

  curveUpdateMsg.subscribe(Messaging::CURVE_UPDATE,
                           [=](uint32_t param) { update(); });
}

coord_t Curve::getPointX(int x) const
{
  return dx +
         limit<coord_t>(0, dw / 2 + divRoundClosest(x * dw / 2, RESX), dw - 1);
}

coord_t Curve::getPointY(int y) const
{
  return dy +
         limit<coord_t>(0, dh / 2 - divRoundClosest(y * dh / 2, RESX), dh - 1);
}

void Curve::updatePosition()
{
  if (positionFunc && positionValue && posPoint && posVLine && posHLine) {
    int valueX = positionFunc();
    int valueY = valueFunc(valueX);

    char coords[16];
    strAppendSigned(
        strAppend(strAppendSigned(coords, calcRESXto100(valueX)), ","),
        calcRESXto100(valueY));

    positionValue->setText(coords);

    lv_coord_t x = getPointX(valueX);
    lv_coord_t y = getPointY(valueY);

    lv_obj_set_pos(posPoint, x - POS_PT_SZ / 2, y - POS_PT_SZ / 2);

    posLinePoints[0] = {x, dy};
    posLinePoints[1] = {x, (lv_coord_t)(dy + dh - 1)};
    posLinePoints[2] = {dx, y};
    posLinePoints[3] = {(lv_coord_t)(dx + dw - 1), y};

    lv_line_set_points(posVLine, &posLinePoints[0], 2);
    lv_line_set_points(posHLine, &posLinePoints[2], 2);
  }
}

void Curve::update()
{
  if (!withLive([](LiveWindow&) { return true; })) return;
  base.update();
  updatePosition();
}

void Curve::onLiveCheckEvents(Window::LiveWindow& live)
{
  // Redraw if crosshair position has changed
  if (positionFunc) {
    int pos = positionFunc();
    if (pos != lastPos) {
      lastPos = pos;
      updatePosition();
    }
  }

  Window::onLiveCheckEvents(live);
}
