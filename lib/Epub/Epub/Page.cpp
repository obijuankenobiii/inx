/**
 * @file Page.cpp
 * @brief Definitions for Page.
 */

#include "Page.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <ImageRender.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <Utf8.h>

#include <climits>
#include <cmath>
#include <algorithm>

#include "../../../src/images/Hr.h"

namespace {

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
                      ImageRenderMode ) {
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
 * Renders a small-caps line using the active body font; small-caps are synthesized from that font.
 */
void PageSmallCaps::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                           ImageRenderMode ) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

bool PageSmallCaps::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, compatFontId);
  return block->serialize(file);
}

std::unique_ptr<PageSmallCaps> PageSmallCaps::deserialize(FsFile& file) {
  int16_t x, y;
  int scId = 0;
  serialization::readPod(file, x);
  serialization::readPod(file, y);
  serialization::readPod(file, scId);
  auto tb = TextBlock::deserialize(file);
  return std::unique_ptr<PageSmallCaps>(new PageSmallCaps(std::move(tb), x, y, scId));
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
                        ImageRenderMode ) {
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
                         ImageRenderMode ) {
  // The drop cap and the first body line both start at yPos, but their fonts have different space above the
  // caps (ascender - glyph.top). Align the drop cap's cap-top with the body cap-top by that inset difference.
  const uint8_t* p = reinterpret_cast<const uint8_t*>(text.c_str());
  const uint32_t dropCp = utf8NextCodepoint(&p);
  const int dropInset = renderer.text.getGlyphTopInset(dropCapFontId, dropCp, EpdFontFamily::BOLD);
  const int bodyInset = renderer.text.getGlyphTopInset(fontId, 'H', EpdFontFamily::REGULAR);
  const int alignY = yPos + yOffset + (bodyInset - dropInset);
  renderer.text.render(dropCapFontId, xPos + xOffset, alignY, text.c_str(), EpdFontFamily::BOLD);
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
                       const ImageRenderMode imageMode) {
  (void)xOffset;
  const int screenW = renderer.getScreenWidth();
  int renderX = (screenW - width) / 2;
  // The viewport top margin is reduced by the font's glyph top inset so text starts at the visual margin
  // (glyphs carry that whitespace themselves). Images have no such whitespace, so add the inset back here —
  // otherwise an image at the top of a page sits flush against the margin with no gap above it.
  const int topInset = renderer.text.getGlyphTopInset(fontId, 'H', EpdFontFamily::REGULAR);
  int renderY = yPos + yOffset + topInset;
  if (renderX < 0) renderX = 0;
  if (renderY < 0) renderY = 0;

  const ImageRender image = ImageRender::create(renderer, cachePath);
  if (!image.render(renderX, renderY, width, height, imageMode)) {
    Serial.printf("[PAGEIMG] Failed to draw image: %s\n", cachePath.c_str());
  }
}

void PageTable::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                       ImageRenderMode) {
  const int originX = xPos + xOffset;
  const int originY = yPos + yOffset;
  // Use the effective line height baked at layout time (respects the line-spacing setting); fall back
  // to the raw font line height for older caches that didn't store it.
  const int lineHeight = this->lineHeight > 0 ? this->lineHeight : renderer.text.getLineHeight(fontId);
  constexpr int kCellPadX = 4;
  constexpr int kCellPadY = 3;

  if (showBorders) {
    renderer.rectangle.render(originX, originY, tableWidth, tableHeight, true, false);
  }

  int yCursor = originY;
  for (size_t rowIndex = 0; rowIndex < rows.size() && rowIndex < rowHeights.size(); ++rowIndex) {
    const auto& row = rows[rowIndex];
    const int rowHeight = rowHeights[rowIndex];
    int xCursor = originX;

    if (showBorders && rowIndex > 0) {
      renderer.line.render(originX, yCursor, originX + tableWidth - 1, yCursor, true);
    }

    size_t gridCol = 0;
    for (size_t cellIndex = 0; cellIndex < row.size() && gridCol < columnWidths.size(); ++cellIndex) {
      const auto& cell = row[cellIndex];
      int span = std::max<uint16_t>(1, cell.colspan);
      if (gridCol + static_cast<size_t>(span) > columnWidths.size()) {
        span = static_cast<int>(columnWidths.size() - gridCol);
      }
      int colWidth = 0;
      for (int s = 0; s < span; ++s) {
        colWidth += columnWidths[gridCol + s];
      }

      if (showBorders && gridCol > 0) {
        renderer.line.render(xCursor, yCursor, xCursor, yCursor + rowHeight, true);
      }

      const auto style = cell.header ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      int textY = yCursor + kCellPadY;
      for (const auto& line : cell.lines) {
        renderer.text.render(fontId, xCursor + kCellPadX, textY, line.c_str(), true, style);
        textY += lineHeight;
        if (textY > yCursor + rowHeight - kCellPadY) {
          break;
        }
      }

      xCursor += colWidth;
      gridCol += span;
    }

    yCursor += rowHeight;
  }
}

void PageHorizontalRule::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                                ImageRenderMode) {
  (void)fontId;
  const int renderX = xPos + xOffset;
  const int renderY = yPos + yOffset;
  renderer.bitmap.icon(Hr, renderX, renderY, WIDTH, HEIGHT);
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

bool PageTable::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, tableWidth);
  serialization::writePod(file, tableHeight);
  serialization::writePod(file, lineHeight);
  serialization::writePod(file, static_cast<uint8_t>(showBorders ? 1 : 0));
  serialization::writePod(file, static_cast<uint16_t>(columnWidths.size()));
  for (const auto width : columnWidths) {
    serialization::writePod(file, width);
  }
  serialization::writePod(file, static_cast<uint16_t>(rowHeights.size()));
  for (const auto height : rowHeights) {
    serialization::writePod(file, height);
  }
  serialization::writePod(file, static_cast<uint16_t>(rows.size()));
  for (const auto& row : rows) {
    serialization::writePod(file, static_cast<uint16_t>(row.size()));
    for (const auto& cell : row) {
      serialization::writePod(file, static_cast<uint8_t>(cell.header ? 1 : 0));
      serialization::writePod(file, static_cast<uint16_t>(cell.colspan));
      serialization::writePod(file, static_cast<uint16_t>(cell.lines.size()));
      for (const auto& line : cell.lines) {
        serialization::writeString(file, line);
      }
    }
  }
  return true;
}

bool PageHorizontalRule::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  return true;
}

std::unique_ptr<PageTable> PageTable::deserialize(FsFile& file) {
  int16_t x = 0, y = 0, width = 0, height = 0;
  serialization::readPod(file, x);
  serialization::readPod(file, y);
  serialization::readPod(file, width);
  serialization::readPod(file, height);
  int16_t lineHeight = 0;
  serialization::readPod(file, lineHeight);
  uint8_t showBordersValue = 0;
  serialization::readPod(file, showBordersValue);

  uint16_t colCount = 0;
  serialization::readPod(file, colCount);
  std::vector<uint16_t> colWidths(colCount);
  for (auto& colWidth : colWidths) {
    serialization::readPod(file, colWidth);
  }

  uint16_t rowHeightCount = 0;
  serialization::readPod(file, rowHeightCount);
  std::vector<uint16_t> rowHeights(rowHeightCount);
  for (auto& rowHeight : rowHeights) {
    serialization::readPod(file, rowHeight);
  }

  uint16_t rowCount = 0;
  serialization::readPod(file, rowCount);
  std::vector<std::vector<PageTable::Cell>> rows(rowCount);
  for (auto& row : rows) {
    uint16_t cellCount = 0;
    serialization::readPod(file, cellCount);
    row.resize(cellCount);
    for (auto& cell : row) {
      uint8_t header = 0;
      serialization::readPod(file, header);
      cell.header = header != 0;
      uint16_t colspan = 1;
      serialization::readPod(file, colspan);
      cell.colspan = colspan < 1 ? 1 : colspan;
      uint16_t lineCount = 0;
      serialization::readPod(file, lineCount);
      cell.lines.resize(lineCount);
      for (auto& line : cell.lines) {
        serialization::readString(file, line);
      }
    }
  }
  return std::unique_ptr<PageTable>(new PageTable(std::move(rows), std::move(colWidths), std::move(rowHeights),
                                                  showBordersValue != 0, width, height, lineHeight, x, y));
}

std::unique_ptr<PageHorizontalRule> PageHorizontalRule::deserialize(FsFile& file) {
  int16_t x = 0;
  int16_t y = 0;
  serialization::readPod(file, x);
  serialization::readPod(file, y);
  return std::unique_ptr<PageHorizontalRule>(new PageHorizontalRule(x, y));
}

void PageCssBorderLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                               ImageRenderMode) {
  (void)fontId;
  const int left = xPos + xOffset;
  const int top = yPos + yOffset;
  const int drawWidth = std::max<int>(1, width);
  const int drawThickness = std::max<int>(1, thickness);
  for (int i = 0; i < drawThickness; ++i) {
    renderer.line.render(left, top + i, left + drawWidth - 1, top + i, true);
  }
}

bool PageCssBorderLine::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, width);
  serialization::writePod(file, thickness);
  return true;
}

std::unique_ptr<PageCssBorderLine> PageCssBorderLine::deserialize(FsFile& file) {
  int16_t x = 0, y = 0, width = 0, thickness = 1;
  serialization::readPod(file, x);
  serialization::readPod(file, y);
  serialization::readPod(file, width);
  serialization::readPod(file, thickness);
  return std::unique_ptr<PageCssBorderLine>(new PageCssBorderLine(x, y, width, thickness));
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
  (void)xOffset;  // images are horizontally centered, not offset by the left margin
  const int screenW = renderer.getScreenWidth();
  bool found = false;
  int minX = INT_MAX;
  int minY = INT_MAX;
  int maxX = INT_MIN;
  int maxY = INT_MIN;
  for (const auto& element : elements) {
    if (element->getTag() != TAG_PageImage) {
      continue;
    }
    // Match PageImage::render: centered horizontally, placed at yPos + yOffset, at the stored size.
    const auto& img = static_cast<const PageImage&>(*element);
    const int rx = std::max(0, (screenW - img.getWidth()) / 2);
    const int ry = std::max(0, img.yPos + yOffset);
    minX = std::min(minX, rx);
    minY = std::min(minY, ry);
    maxX = std::max(maxX, rx + img.getWidth());
    maxY = std::max(maxY, ry + img.getHeight());
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

void Page::render(GfxRenderer& renderer, const int fontId, const int headerFontId, const int xOffset,
                  const int yOffset, bool skipImages, const ImageRenderMode imageMode) const {
  for (auto& element : elements) {
    if (skipImages && element->getTag() == TAG_PageImage) {
      continue;
    }

    uint8_t tag = element->getTag();
    if (tag == TAG_PageLine) {
      const auto* line = static_cast<const PageLine*>(element.get());
      line->getTextBlock().render(renderer, fontId, line->xPos + xOffset, line->yPos + yOffset);
    } else if (tag == TAG_PageHeader) {
      const auto* header = static_cast<const PageHeader*>(element.get());
      header->getTextBlock().render(renderer, headerFontId, header->xPos + xOffset, header->yPos + yOffset);
    } else {
      element->render(renderer, fontId, xOffset, yOffset, imageMode);
    }
  }
}

void Page::renderImages(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset,
                        const ImageRenderMode imageMode) const {
  for (auto& element : elements) {
    if (element->getTag() != TAG_PageImage) {
      continue;
    }
    element->render(renderer, fontId, xOffset, yOffset, imageMode);
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
    } else if (tag == TAG_PageSmallCaps) {
      page->elements.push_back(PageSmallCaps::deserialize(file));
    } else if (tag == TAG_PageHeader) {
      page->elements.push_back(PageHeader::deserialize(file));
    } else if (tag == TAG_PageImage) {
      page->elements.push_back(PageImage::deserialize(file));
    } else if (tag == TAG_PageDropCap) {
      page->elements.push_back(PageDropCap::deserialize(file));
    } else if (tag == TAG_PageTable) {
      page->elements.push_back(PageTable::deserialize(file));
    } else if (tag == TAG_PageHorizontalRule) {
      page->elements.push_back(PageHorizontalRule::deserialize(file));
    } else if (tag == TAG_PageCssBorderLine) {
      page->elements.push_back(PageCssBorderLine::deserialize(file));
    }
  }
  return page;
}
