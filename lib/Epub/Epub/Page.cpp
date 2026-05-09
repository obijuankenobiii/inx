/**
 * @file Page.cpp
 * @brief Definitions for Page.
 */

#include "Page.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <JpegRender.h>
#include <PngRender.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include "../../../src/util/StringUtils.h"

#include <climits>
#include <cmath>
#include <algorithm>

namespace {

constexpr float kImgScaleEps = 1e-5f;
constexpr float kImgHuge = 1e9f;

enum class PageImageFormat { Bitmap, Jpeg, Png };

PageImageFormat imageFormatFromPath(const std::string& path) {
  if (StringUtils::checkFileExtension(path, ".jpg") || StringUtils::checkFileExtension(path, ".jpeg")) {
    return PageImageFormat::Jpeg;
  }
  if (StringUtils::checkFileExtension(path, ".png")) {
    return PageImageFormat::Png;
  }
  return PageImageFormat::Bitmap;
}

bool getImageSourceDimensions(const std::string& path, PageImageFormat format, int* outW, int* outH) {
  if (format == PageImageFormat::Jpeg) {
    return JpegRender::getDimensions(path, outW, outH);
  }
  if (format == PageImageFormat::Png) {
    return PngRender::getDimensions(path, outW, outH);
  }

  FsFile file;
  if (!SdMan.openFileForRead("EHP", path, file)) {
    return false;
  }
  Bitmap bitmap(file, BitmapDitherMode::None);
  const bool ok = bitmap.parseHeaders() == BmpReaderError::Ok;
  if (ok) {
    *outW = bitmap.getWidth();
    *outH = bitmap.getHeight();
  }
  file.close();
  return ok;
}

bool renderPageImageByFormat(GfxRenderer& renderer, const std::string& path, PageImageFormat format, int x, int y,
                             int width, int height, BitmapDitherMode imageDitherMode) {
  if (format == PageImageFormat::Jpeg) {
    JpegRender jpeg(renderer);
    return jpeg.fromPath(path, x, y, width, height, false);
  }
  if (format == PageImageFormat::Png) {
    PngRender png(renderer);
    return png.fromPath(path, x, y, width, height);
  }

  FsFile file;
  if (!SdMan.openFileForRead("EHP", path, file)) {
    Serial.printf("[PAGEIMG] Failed to open image file: %s\n", path.c_str());
    return false;
  }

  Bitmap bitmap(file, imageDitherMode);
  const bool ok = bitmap.parseHeaders() == BmpReaderError::Ok;
  if (ok) {
    renderer.bitmap.render(bitmap, x, y, width, height);
  }
  file.close();
  return ok;
}

/**
 * Tight pixel bounds for how PageImage::render + GfxRenderer::drawBitmap place the bitmap (fit within layout w/h).
 * Matches drawBitmap1Bit / drawBitmap scale and non-replicate vs replicate-upscale branches.
 */
bool pageImagePaintBounds(const PageImage& img, const GfxRenderer& renderer, int xOffset, int yOffset, int& outX1,
                          int& outY1, int& outX2, int& outY2) {
  (void)xOffset;
  const PageImageFormat format = imageFormatFromPath(img.getPath());
  int sourceW = 0;
  int sourceH = 0;
  if (!getImageSourceDimensions(img.getPath(), format, &sourceW, &sourceH)) return false;

  const int lw = img.getWidth();
  const int lh = img.getHeight();
  if (sourceW <= 0 || sourceH <= 0 || lw <= 0 || lh <= 0) {
    return false;
  }

  const int screenW = renderer.getScreenWidth();
  int renderX = (screenW - lw) / 2;
  if (renderX < 0) {
    renderX = 0;
  }
  int renderY = img.yPos + yOffset;
  if (renderY < 0) {
    renderY = 0;
  }

  const bool hasW = lw > 0;
  const bool hasH = lh > 0;
  float scale = 1.0f;
  bool isScaled = false;
  if (hasW || hasH) {
    const float fitW = hasW ? static_cast<float>(lw) / static_cast<float>(sourceW) : kImgHuge;
    const float fitH = hasH ? static_cast<float>(lh) / static_cast<float>(sourceH) : kImgHuge;
    const float fitScale = (hasW && hasH) ? std::min(fitW, fitH) : (hasW ? fitW : fitH);
    if (std::abs(fitScale - 1.0f) > kImgScaleEps) {
      scale = fitScale;
      isScaled = true;
    }
  }
  const bool replicateUpscale = isScaled && scale > 1.0f + kImgScaleEps;

  int x2 = renderX;
  int y2 = renderY;
  if (replicateUpscale) {
    x2 = renderX + static_cast<int>(std::ceil(static_cast<float>(sourceW) * scale)) - 1;
    y2 = renderY + static_cast<int>(std::ceil(static_cast<float>(sourceH) * scale)) - 1;
  } else if (isScaled) {
    x2 = renderX + static_cast<int>(std::floor(static_cast<float>(sourceW - 1) * scale));
    y2 = renderY + static_cast<int>(std::floor(static_cast<float>(sourceH - 1) * scale));
  } else {
    x2 = renderX + sourceW - 1;
    y2 = renderY + sourceH - 1;
  }

  outX1 = std::max(0, std::min(renderX, screenW - 1));
  outY1 = std::max(0, renderY);
  outX2 = std::min(screenW - 1, std::max(outX1, x2));
  outY2 = std::max(outY1, y2);
  return true;
}

}  

/**
 * Renders a text line on the screen.
 *
 * @param renderer The graphics renderer
 * @param fontId Base font ID for text rendering
 * @param xOffset Horizontal offset for page margins
 * @param yOffset Vertical offset for page margins
 */
void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                      BitmapDitherMode ) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}
/**
 * Serializes a PageLine to a file.
 *
 * @param file The file to write to
 * @return true if serialization was successful
 */
bool PageLine::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  return block->serialize(file);
}

/**
 * Deserializes a PageLine from a file.
 *
 * @param file The file to read from
 * @return Unique pointer to the deserialized PageLine
 */
std::unique_ptr<PageLine> PageLine::deserialize(FsFile& file) {
  int16_t x, y;
  serialization::readPod(file, x);
  serialization::readPod(file, y);
  auto tb = TextBlock::deserialize(file);
  return std::unique_ptr<PageLine>(new PageLine(std::move(tb), x, y));
}

/**
 * Renders a header on the screen.
 * Uses the stored headerFontId for rendering.
 *
 * @param renderer The graphics renderer
 * @param fontId Ignored (kept for interface)
 * @param xOffset Horizontal offset for page margins
 * @param yOffset Vertical offset for page margins
 */
void PageHeader::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                        BitmapDitherMode ) {
  block->render(renderer, headerFontId, xPos + xOffset, yPos + yOffset);
}

/**
 * Serializes a PageHeader to a file.
 *
 * @param file The file to write to
 * @return true if serialization was successful
 */
bool PageHeader::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, headerFontId);
  return block->serialize(file);
}

/**
 * Deserializes a PageHeader from a file.
 * Reads headerFontId from the file.
 *
 * @param file The file to read from
 * @return Unique pointer to the deserialized PageHeader
 */
std::unique_ptr<PageHeader> PageHeader::deserialize(FsFile& file) {
  int16_t x, y;
  int headerId = 0;
  serialization::readPod(file, x);
  serialization::readPod(file, y);
  if (file.available()) {
    serialization::readPod(file, headerId);
  }
  auto textBlock = TextBlock::deserialize(file);
  return std::unique_ptr<PageHeader>(new PageHeader(std::move(textBlock), x, y, headerId));
}

/**
 * Renders a drop cap on the screen.
 * Uses a specific large font and renders the single character at the start of a paragraph.
 */
void PageDropCap::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                         BitmapDitherMode ) {
  renderer.text.render(dropCapFontId, xPos + xOffset, yPos + yOffset - 5, text.c_str(), EpdFontFamily::BOLD);
}

/**
 * Serializes a PageDropCap to a file.
 *
 * @param file The file to write to
 * @return true if serialization was successful
 */
bool PageDropCap::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, dropCapFontId);
  serialization::writeString(file, text);
  return true;
}

/**
 * Serializes a PageDropCap to a file.
 *
 * @param file The file to write to
 * @return true if serialization was successful
 */
std::unique_ptr<PageDropCap> PageDropCap::deserialize(FsFile& file) {
  int16_t x, y;
  int dcFontId;
  std::string text;
  serialization::readPod(file, x);
  serialization::readPod(file, y);
  serialization::readPod(file, dcFontId);
  serialization::readString(file, text);
  return std::unique_ptr<PageDropCap>(new PageDropCap(text, x, y, dcFontId));
}

/**
 * Renders an image on the screen.
 * Scales the image to fit within the available content area while maintaining aspect ratio.
 * Centers the image horizontally within the margins.
 *
 * @param renderer The graphics renderer
 * @param fontId Unused parameter (kept for interface compatibility)
 * @param xOffset Horizontal offset for page margins
 * @param yOffset Vertical offset for page margins
 */
void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                       const BitmapDitherMode imageDitherMode) {
  (void)xOffset;  
  (void)fontId;
  const PageImageFormat format = imageFormatFromPath(cachePath);
  const int screenW = renderer.getScreenWidth();
  int renderX = (screenW - width) / 2;
  int renderY = yPos + yOffset;
  if (renderX < 0) renderX = 0;
  if (renderY < 0) renderY = 0;

  if (!renderPageImageByFormat(renderer, cachePath, format, renderX, renderY, width, height, imageDitherMode)) {
    Serial.printf("[PAGEIMG] Failed to draw image: %s\n", cachePath.c_str());
  }
}

/**
 * Serializes a PageImage to a file.
 *
 * @param file The file to write to
 * @return true if serialization was successful
 */
bool PageImage::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, width);
  serialization::writePod(file, height);
  serialization::writeString(file, cachePath);
  return true;
}

/**
 * Deserializes a PageImage from a file.
 *
 * @param file The file to read from
 * @return Unique pointer to the deserialized PageImage
 */
std::unique_ptr<PageImage> PageImage::deserialize(FsFile& file) {
  int16_t x, y, w, h;
  serialization::readPod(file, x);
  serialization::readPod(file, y);
  serialization::readPod(file, w);
  serialization::readPod(file, h);
  std::string path;
  serialization::readString(file, path);
  return std::unique_ptr<PageImage>(new PageImage(path, w, h, x, y));
}

/**
 * Renders all elements on the page.
 *
 * @param renderer The graphics renderer
 * @param fontId Font ID for text rendering
 * @param headerFontId Font ID for header rendering
 * @param xOffset Horizontal offset for page margins
 * @param yOffset Vertical offset for page margins
 * @param skipImages If true, images are not rendered
 */
bool Page::getImageBoundingBox(const GfxRenderer& renderer, const int xOffset, const int yOffset, int16_t& outX,
                               int16_t& outY, int16_t& outW, int16_t& outH) const {
  bool found = false;
  int minX = INT_MAX;
  int minY = INT_MAX;
  int maxX = INT_MIN;
  int maxY = INT_MIN;
  for (const auto& element : elements) {
    if (element->getTag() != TAG_PageImage) {
      continue;
    }
    const auto& img = static_cast<const PageImage&>(*element);
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
    if (!pageImagePaintBounds(img, renderer, xOffset, yOffset, x1, y1, x2, y2)) {
      const int screenW = renderer.getScreenWidth();
      int rx = (screenW - img.getWidth()) / 2;
      if (rx < 0) {
        rx = 0;
      }
      int ry = img.yPos + yOffset;
      if (ry < 0) {
        ry = 0;
      }
      x1 = rx;
      y1 = ry;
      x2 = rx + std::max(0, static_cast<int>(img.getWidth()) - 1);
      y2 = ry + std::max(0, static_cast<int>(img.getHeight()) - 1);
    }
    minX = std::min(minX, x1);
    minY = std::min(minY, y1);
    maxX = std::max(maxX, x2 + 1);
    maxY = std::max(maxY, y2 + 1);
    found = true;
  }
  if (!found || maxX <= minX || maxY <= minY) {
    return false;
  }
  outX = static_cast<int16_t>(minX);
  outY = static_cast<int16_t>(minY);
  outW = static_cast<int16_t>(maxX - minX);
  outH = static_cast<int16_t>(maxY - minY);
  return true;
}

void Page::render(GfxRenderer& renderer, const int fontId, const int headerFontId, const int xOffset, const int yOffset,
                  bool skipImages, const BitmapDitherMode imageDitherMode) const {
  for (auto& element : elements) {
    if (skipImages && element->getTag() == TAG_PageImage) {
      continue;
    }

    uint8_t tag = element->getTag();
    if (tag == TAG_PageHeader) {
      element->render(renderer, headerFontId, xOffset, yOffset, imageDitherMode);
    } else {
      element->render(renderer, fontId, xOffset, yOffset, imageDitherMode);
    }
  }
}

void Page::renderImages(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                        const BitmapDitherMode imageDitherMode) const {
  for (auto& element : elements) {
    if (element->getTag() != TAG_PageImage) {
      continue;
    }
    element->render(renderer, fontId, xOffset, yOffset, imageDitherMode);
  }
}

/**
 * Serializes a Page to a file.
 *
 * @param file The file to write to
 * @return true if serialization was successful
 */
bool Page::serialize(FsFile& file) const {
  const uint16_t count = elements.size();
  serialization::writePod(file, count);
  for (const auto& el : elements) {
    serialization::writePod(file, static_cast<uint8_t>(el->getTag()));
    if (!el->serialize(file)) return false;
  }
  return true;
}

/**
 * Deserializes a Page from a file.
 *
 * @param file The file to read from
 * @return Unique pointer to the deserialized Page
 */
std::unique_ptr<Page> Page::deserialize(FsFile& file) {
  auto page = std::unique_ptr<Page>(new Page());
  uint16_t count;
  serialization::readPod(file, count);
  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag;
    serialization::readPod(file, tag);
    if (tag == TAG_PageLine) {
      page->elements.push_back(PageLine::deserialize(file));
    } else if (tag == TAG_PageHeader) {
      page->elements.push_back(PageHeader::deserialize(file));
    } else if (tag == TAG_PageImage) {
      page->elements.push_back(PageImage::deserialize(file));
    } else if (tag == TAG_PageDropCap) {
      page->elements.push_back(PageDropCap::deserialize(file));
    }
  }
  return page;
}
