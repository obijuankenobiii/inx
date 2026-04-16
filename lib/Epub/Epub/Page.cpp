#include "Page.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

/**
 * Renders a text line on the screen.
 *
 * @param renderer The graphics renderer
 * @param fontId Base font ID for text rendering
 * @param xOffset Horizontal offset for page margins
 * @param yOffset Vertical offset for page margins
 */
void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
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
/**
 * Renders a header on the screen.
 * Uses the stored headerFontId for rendering.
 *
 * @param renderer The graphics renderer
 * @param fontId Ignored (kept for interface)
 * @param xOffset Horizontal offset for page margins
 * @param yOffset Vertical offset for page margins
 */
void PageHeader::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
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
void PageDropCap::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  renderer.drawText(dropCapFontId, xPos + xOffset, yPos + yOffset - 5, text.c_str(), EpdFontFamily::BOLD);
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
void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {  
  FsFile file;
  if (!SdMan.openFileForRead("EHP", cachePath, file)) {
    Serial.printf("[PAGEIMG] Failed to open image file: %s\n", cachePath.c_str());
    return;
  }

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() == BmpReaderError::Ok) {
    int screenW = renderer.getScreenWidth();
    int screenH = renderer.getScreenHeight();
    
    int renderX = (screenW - width) / 2;
    int renderY = yPos + yOffset;

    if (renderX < 0) renderX = 0;
    if (renderY < 0) renderY = 0;
    
    renderer.drawBitmap(bitmap, renderX, renderY, width, height);
  }

  file.close();
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
void Page::render(GfxRenderer& renderer, const int fontId, const int headerFontId, const int xOffset, const int yOffset,
                  bool skipImages) const {  
  for (auto& element : elements) {
    if (skipImages && element->getTag() == TAG_PageImage) {
      continue;
    }

    uint8_t tag = element->getTag();
    if (tag == TAG_PageHeader) {      
      element->render(renderer, headerFontId, xOffset, yOffset);
    } else {
      element->render(renderer, fontId, xOffset,yOffset);
    }

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