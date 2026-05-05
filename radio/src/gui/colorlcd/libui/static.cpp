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

#include "static.h"

#include "bitmaps.h"
#include "debug.h"
#include "mainwindow.h"
#include "lz4/lz4.h"
#include "sdcard.h"
#include "etx_lv_theme.h"
#include "stb/stb_image.h"

//-----------------------------------------------------------------------------

static uint16_t read_u16_le(const uint8_t* data)
{
  return uint16_t(data[0]) | (uint16_t(data[1]) << 8);
}

#if defined(SIMU)
static bool forceStaticLZ4ImageBufferAllocationFailure = false;
static bool forceStaticTextCreateFailure = false;

void staticLZ4ImageForceBufferAllocationFailureForTest(bool force)
{
  forceStaticLZ4ImageBufferAllocationFailure = force;
}
#endif

static uint8_t* allocStaticLZ4ImageBuffer(size_t size)
{
#if defined(SIMU)
  if (forceStaticLZ4ImageBufferAllocationFailure) return nullptr;
#endif
  return static_cast<uint8_t*>(lv_mem_alloc(size));
}

static lv_obj_t* createStaticTextObject(lv_obj_t* parent)
{
#if defined(SIMU)
  if (forceStaticTextCreateFailure) return nullptr;
#endif
  return lv_label_create(parent);
}

#if defined(SIMU)
static bool forceStaticImageCreateFailure = false;

void staticImageForceImageCreateFailureForTest(bool force)
{
  forceStaticImageCreateFailure = force;
}
#endif

static lv_obj_t* createStaticImageObject(lv_obj_t* parent)
{
#if defined(SIMU)
  if (forceStaticImageCreateFailure) return nullptr;
#endif
  return lv_img_create(parent);
}

//-----------------------------------------------------------------------------

StaticText::StaticText(Window* parent, const rect_t& rect, std::string txt,
                       LcdColorIndex color, LcdFlags textFlags) :
    Window(parent, rect, createStaticTextObject), text(std::move(txt))
{
  setTextFlag(textFlags);
  setWindowFlag(NO_FOCUS);
  if (!hasLvObj()) return;

  etx_font(lvobj, FONT_INDEX(textFlags));
  etx_txt_color(lvobj, color);

  if (textFlags & CENTERED)
    etx_obj_add_style(lvobj, styles->text_align_center, LV_PART_MAIN);
  else if (textFlags & RIGHT)
    etx_obj_add_style(lvobj, styles->text_align_right, LV_PART_MAIN);

  lv_obj_set_style_grid_cell_x_align(lvobj, LV_GRID_ALIGN_STRETCH,
                                     LV_PART_MAIN);
  lv_label_set_text(lvobj, text.c_str());
  if (rect.h == 0) lv_obj_set_height(lvobj, LV_SIZE_CONTENT);
}

#if defined(DEBUG_WINDOWS)
std::string StaticText::getName() const
{
  return "StaticText \"" + text.substr(0, 20) + "\"";
}
#endif

void StaticText::setText(std::string value)
{
  if (text != value) {
    text = std::move(value);
    withLvObj([&](lv_obj_t* obj) {
      lv_label_set_text(obj, text.c_str());
    });
  }
}

const std::string& StaticText::getText() const { return text; }

#if defined(SIMU)
std::string StaticText::automationText() const
{
  auto label = Window::automationText();
  if (!label.empty()) return label;
  return text;
}
#endif

//-----------------------------------------------------------------------------

template <>
void DynamicNumber<uint32_t>::updateText()
{
  if (lvobj) {
    const char* p = prefix ? prefix : "";
    const char* s = suffix ? suffix : "";
    if ((textFlags & PREC2) == PREC2) {
      lv_label_set_text_fmt(lvobj, "%s%" PRIu32 ".%02" PRIu32 "%s", p,
                            value / 100, value % 100, s);
    } else if (textFlags & PREC1) {
      lv_label_set_text_fmt(lvobj, "%s%" PRIu32 ".%01" PRIu32 "%s", p,
                            value / 10, value % 10, s);
    } else {
      lv_label_set_text_fmt(lvobj, "%s%" PRIu32 "%s", p, value, s);
    }
  }
}

template <>
void DynamicNumber<int32_t>::updateText()
{
  if (lvobj) {
    const char* p = prefix ? prefix : "";
    const char* s = suffix ? suffix : "";
    if ((textFlags & PREC2) == PREC2) {
      lv_label_set_text_fmt(lvobj, "%s%" PRId32 ".%02" PRIu32 "%s", p,
                            value / 100, (uint32_t)abs(value % 100), s);
    } else if (textFlags & PREC1) {
      lv_label_set_text_fmt(lvobj, "%s%" PRId32 ".%01" PRIu32 "%s", p,
                            value / 10, (uint32_t)abs(value % 10), s);
    } else {
      lv_label_set_text_fmt(lvobj, "%s%" PRId32 "%s", p, value, s);
    }
  }
}

template <>
void DynamicNumber<uint16_t>::updateText()
{
  if (lvobj) {
    const char* p = prefix ? prefix : "";
    const char* s = suffix ? suffix : "";
    if ((textFlags & PREC2) == PREC2) {
      lv_label_set_text_fmt(lvobj, "%s%" PRIu16 ".%02" PRIu16 "%s", p,
                            (uint16_t)(value / 100), (uint16_t)(value % 100),
                            s);
    } else if (textFlags & PREC1) {
      lv_label_set_text_fmt(lvobj, "%s%" PRIu16 ".%01" PRIu16 "%s", p,
                            (uint16_t)(value / 10), (uint16_t)(value % 10), s);
    } else {
      lv_label_set_text_fmt(lvobj, "%s%" PRIu16 "%s", p, value, s);
    }
  }
}

template <>
void DynamicNumber<int16_t>::updateText()
{
  if (lvobj) {
    const char* p = prefix ? prefix : "";
    const char* s = suffix ? suffix : "";
    if ((textFlags & PREC2) == PREC2) {
      lv_label_set_text_fmt(lvobj, "%s%" PRId16 ".%02" PRIu16 "%s", p,
                            (int16_t)(value / 100), (uint16_t)abs(value % 100),
                            s);
    } else if (textFlags & PREC1) {
      lv_label_set_text_fmt(lvobj, "%s%" PRId16 ".%01" PRIu16 "%s", p,
                            (int16_t)(value / 10), (uint16_t)abs(value % 10),
                            s);
    } else {
      lv_label_set_text_fmt(lvobj, "%s%" PRId16 "%s", p, value, s);
    }
  }
}

//-----------------------------------------------------------------------------

StaticIcon::StaticIcon(Window* parent, coord_t x, coord_t y, EdgeTxIcon icon,
                       LcdColorIndex color) :
    Window(parent, rect_t{x, y, 0, 0}, lv_canvas_create),
    currentColor(color)
{
  setWindowFlag(NO_FOCUS | NO_CLICK);
  if (!hasLvObj()) return;

  setIcon(icon);

  etx_img_color(lvobj, currentColor, LV_PART_MAIN);
}

StaticIcon::StaticIcon(Window* parent, coord_t x, coord_t y, const char* filename,
                       LcdColorIndex color) :
    Window(parent, rect_t{x, y, 0, 0}, lv_canvas_create),
    currentColor(color)
{
  setWindowFlag(NO_FOCUS | NO_CLICK);
  if (!hasLvObj()) return;

  auto bm = BitmapBuffer::loadBitmap(filename, BMP_RGB565);
  if (bm) {
    size_t size;
    mask = bm->to8bitMask(&size);
    if (mask) {
      setSize(mask->width, mask->height);
      lv_canvas_set_buffer(lvobj, (void*)mask->data, mask->width, mask->height,
                           LV_IMG_CF_ALPHA_8BIT);
    }
    delete bm;
  }

  etx_img_color(lvobj, currentColor, LV_PART_MAIN);
}

void StaticIcon::deleteLater()
{
  if (_deleted) return;
  if (mask) free(mask);
  mask = nullptr;
  Window::deleteLater();
}

void StaticIcon::setColor(LcdColorIndex color)
{
  if (currentColor != color) {
    withLvObj([&](lv_obj_t* obj) {
      etx_img_color(obj, color, LV_PART_MAIN);
    });
    currentColor = color;
  }
}

void StaticIcon::setIcon(EdgeTxIcon icon)
{
  withLvObj([&](lv_obj_t* obj) {
    auto newMask = getBuiltinIcon(icon);
    setSize(newMask->width, newMask->height);
    lv_canvas_set_buffer(obj, (void*)newMask->data, newMask->width,
                         newMask->height, LV_IMG_CF_ALPHA_8BIT);
  });
}

void StaticIcon::center(coord_t w, coord_t h)
{
  setPos((w - width()) / 2, (h - height()) / 2);
}

//-----------------------------------------------------------------------------

// Display image from file system using LVGL with LVGL scaling
//  - LVGL scaling is slow so don't use this if there are many images
StaticImage::StaticImage(Window* parent, const rect_t& rect,
                         const char* filename, bool fillFrame, bool dontEnlarge) :
    Window(parent, rect), fillFrame(fillFrame), dontEnlarge(dontEnlarge)
{
  setWindowFlag(NO_FOCUS | NO_CLICK);

  if (!filename) filename = "";
  setSource(filename);
}

void StaticImage::setSource(std::string filename)
{
  if (!acceptsEvents()) return;

  if (!filename.empty()) {
    std::string fullpath = std::string("A");
    if (filename[0] != PATH_SEPARATOR[0]) fullpath += PATH_SEPARATOR;
    fullpath += filename;

    if (!image) image = createStaticImageObject(lvobj);
    if (!image) return;

    lv_obj_set_pos(image, 0, 0);
    lv_obj_set_size(image, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(image);
    lv_img_set_src(image, fullpath.c_str());
    if (!hasImage()) {
      // Failed to load
      TRACE_ERROR("could not load image '%s' - %s\n", filename.c_str(), stbi_failure_reason());
      clearSource();
    }
    setZoom();
  } else {
    clearSource();
  }
}

void StaticImage::clearSource()
{
  if (image) lv_obj_del(image);
  image = nullptr;
}

bool StaticImage::hasImage() const
{
  lv_img_t* img = (lv_img_t*)image;
  return img && img->w && img->h;
}

void StaticImage::setZoom()
{
  lv_img_t* img = (lv_img_t*)image;
  if (img && img->w && img->h) {
    uint16_t zw = (width() * 256) / img->w;
    uint16_t zh = (height() * 256) / img->h;
    uint16_t z = fillFrame ? max(zw, zh) : min(zw, zh);
    if (dontEnlarge) z = min(z, (uint16_t)256);
    lv_img_set_zoom(image, z);
  }
}

//-----------------------------------------------------------------------------

// Display image from file system with software scaling
//  - uglier but much faster than LVGL scaling
StaticBitmap::StaticBitmap(Window* parent, const rect_t& rect,
                           const char* filename) :
    Window(parent, rect)
{
  setWindowFlag(NO_FOCUS | NO_CLICK);

  setSource(filename);
}

void StaticBitmap::setSource(const char *filename)
{
  if (!acceptsEvents()) return;

  if (filename) {
    if (filename[0] == '\0') {
      clearSource();
      return;
    }

    BitmapBuffer* newImg = BitmapBuffer::loadBitmap(filename, BMP_ARGB4444);
    if (newImg) {
      newImg->resizeToLVGL(width(), height());
      if (!newImg->getData() || newImg->width() == 0 || newImg->height() == 0) {
        delete newImg;
        return;
      }

      lv_obj_t* newCanvas = lv_canvas_create(lvobj);
      if (!newCanvas) {
        delete newImg;
        return;
      }

      lv_obj_center(newCanvas);
      lv_canvas_set_buffer(newCanvas, newImg->getData(), newImg->width(), newImg->height(),
                          LV_IMG_CF_TRUE_COLOR_ALPHA);

      auto oldCanvas = canvas;
      auto oldImg = img;
      canvas = newCanvas;
      img = newImg;

      if (oldCanvas) lv_obj_del(oldCanvas);
      if (oldImg) delete oldImg;
    }
  }
}

void StaticBitmap::clearSource()
{
  if (canvas) lv_obj_del(canvas);
  canvas = nullptr;
  if (img) delete img;
  img = nullptr;
}

StaticBitmap::~StaticBitmap()
{
  if (img) delete img;
}

bool StaticBitmap::hasImage() const
{
  return img && canvas;
}

//-----------------------------------------------------------------------------

StaticLZ4Image::StaticLZ4Image(Window* parent, coord_t x, coord_t y,
                               const LZ4Bitmap* lz4Bitmap) :
    Window(parent, {x, y, lz4Bitmap->width, lz4Bitmap->height},
           lv_canvas_create)
{
  setWindowFlag(NO_FOCUS | NO_CLICK);
  if (!hasLvObj()) return;

  // Convert ARGB4444 to LV_IMG_CF_TRUE_COLOR_ALPHA
  uint16_t w = lz4Bitmap->width;
  uint16_t h = lz4Bitmap->height;

  uint32_t pixels = w * h;
  uint32_t size = (pixels + 1) & 0xFFFFFFFE;
  imgData = allocStaticLZ4ImageBuffer(size * 3);
  if (!imgData) return;

  uint8_t* decompData = imgData + size;

  LZ4_decompress_safe((const char*)lz4Bitmap->data, (char*)decompData,
                      lz4Bitmap->compressedSize, pixels * sizeof(uint16_t));

  uint8_t* dest = imgData;
  for (uint32_t i = 0; i < pixels; i += 1) {
    uint16_t c = read_u16_le(decompData);
    ARGB_SPLIT(c, a, r, g, b);
    c = RGB_JOIN(r * 2, g * 4, b * 2);
    *dest++ = c & 0xFF;
    *dest++ = c >> 8;
    *dest++ = (a * 255) / 15;
    decompData += sizeof(uint16_t);
  }

  lv_canvas_set_buffer(lvobj, imgData, w, h, LV_IMG_CF_TRUE_COLOR_ALPHA);
}

#if defined(SIMU)
bool staticTextObjectCreateFailureFailsClosedForTest()
{
  forceStaticTextCreateFailure = true;
  auto text = new (std::nothrow) StaticText(MainWindow::instance(),
                                           {0, 0, 100, 20}, "Title");
  forceStaticTextCreateFailure = false;

  if (!text) return false;
  text->setText("Next");
  bool ok = !text->isAvailable() && !text->isVisible() &&
            text->automationText() == "Next";
  delete text;
  return ok;
}

bool staticLZ4ImageBufferAllocationFailureLeavesNoImageDataForTest()
{
  class TestStaticLZ4Image : public StaticLZ4Image
  {
   public:
    TestStaticLZ4Image(Window* parent, const LZ4Bitmap* bitmap) :
        StaticLZ4Image(parent, 0, 0, bitmap)
    {
    }

    bool hasImageData() const { return imgData != nullptr; }
  };

  alignas(LZ4Bitmap) static const uint8_t lz4Bitmap[] = {
      1, 0, 1, 0, 0, 0, 0, 0};

  staticLZ4ImageForceBufferAllocationFailureForTest(true);
  auto image = new (std::nothrow)
      TestStaticLZ4Image(MainWindow::instance(),
                         reinterpret_cast<const LZ4Bitmap*>(lz4Bitmap));
  staticLZ4ImageForceBufferAllocationFailureForTest(false);

  return image && !image->hasImageData();
}

bool staticImageObjectCreateFailureLeavesNoImageForTest()
{
  class TestStaticImage : public StaticImage
  {
   public:
    TestStaticImage(Window* parent) :
        StaticImage(parent, {0, 0, 32, 32}, BITMAPS_PATH "/missing.png")
    {
    }

    bool hasImageObject() const { return image != nullptr; }
  };

  staticImageForceImageCreateFailureForTest(true);
  auto image = new (std::nothrow) TestStaticImage(MainWindow::instance());
  staticImageForceImageCreateFailureForTest(false);

  return image && !image->hasImageObject();
}
#endif

void StaticLZ4Image::deleteLater()
{
  if (!deleted()) {
    if (imgData) lv_mem_free(imgData);
    imgData = nullptr;
    Window::deleteLater();
  }
}

//-----------------------------------------------------------------------------

QRCode::QRCode(Window *parent, coord_t x, coord_t y, coord_t sz, std::string data,
               LcdFlags color, LcdFlags bgColor) :
    Window(parent, {x, y, sz, sz})
{
  setWindowFlag(NO_CLICK);
  if (!hasLvObj()) return;

  qr = lv_qrcode_create(lvobj, sz, makeLvColor(color), makeLvColor(bgColor));
  if (!requireLvObj(qr)) return;
  setData(data);
}

void QRCode::setData(std::string data)
{
  if (qr)
    lv_qrcode_update(qr, data.c_str(), data.length());
}
