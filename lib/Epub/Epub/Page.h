#pragma once

/**
 * @file Page.h
 * @brief Public interface and types for Page.
 */

#include <Bitmap.h>
#include <SdFat.h>
#include <utility>
#include <vector>
#include <algorithm>

#include "blocks/TextBlock.h"

class GfxRenderer;

enum PageElementTag : uint8_t {
  TAG_PageLine = 1,
  TAG_PageHeader = 2,
  TAG_PageImage = 3,
  TAG_PageDropCap = 4, 
};

/**
 * Base class for all elements that can appear on a page.
 * Provides common position data and virtual interface for rendering and serialization.
 */
class PageElement {
 public:
  int16_t xPos;
  int16_t yPos;
  
  /**
   * Constructs a page element at the specified position.
   * * @param xPos X coordinate on the page
   * @param yPos Y coordinate on the page
   */
  explicit PageElement(const int16_t xPos, const int16_t yPos) : xPos(xPos), yPos(yPos) {}
  virtual ~PageElement() = default;
  
  /**
   * Returns the element type tag for identification.
   * * @return The element type tag
   */
  virtual PageElementTag getTag() const = 0;
  
  /**
   * Renders the element on the screen.
   * * @param renderer The graphics renderer
   * @param fontId Font ID for text rendering
   * @param xOffset Horizontal offset for page margins
   * @param yOffset Vertical offset for page margins
   * @param imageDitherMode High-color BMP decode dithering (images only; callers pass from app settings).
   */
  virtual void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
                      BitmapDitherMode imageDitherMode = BitmapDitherMode::None) = 0;
  
  /**
   * Serializes the element to a file.
   * * @param file The file to write to
   * @return true if serialization was successful
   */
  virtual bool serialize(FsFile& file) = 0;
};

/**
 * Represents a line of normal text on a page.
 * Contains a TextBlock for regular paragraph text.
 */
class PageLine final : public PageElement {
  std::shared_ptr<TextBlock> block;

 public:
  PageLine(std::shared_ptr<TextBlock> block, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), block(std::move(block)) {}

  const TextBlock& getTextBlock() const { return *block; }

  PageElementTag getTag() const override { return TAG_PageLine; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
              BitmapDitherMode imageDitherMode = BitmapDitherMode::None) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageLine> deserialize(FsFile& file);
};

/**
 * Represents a header line on a page.
 * Uses the specified headerFontId for rendering.
 */
class PageHeader final : public PageElement {
  std::shared_ptr<TextBlock> block;
  int headerFontId;

 public:
  PageHeader(std::shared_ptr<TextBlock> block, const int16_t xPos, const int16_t yPos, int fontId)
      : PageElement(xPos, yPos), block(std::move(block)), headerFontId(fontId) {}

  const TextBlock& getTextBlock() const { return *block; }
  int getHeaderFontId() const { return headerFontId; }

  PageElementTag getTag() const override { return TAG_PageHeader; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
              BitmapDitherMode imageDitherMode = BitmapDitherMode::None) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageHeader> deserialize(FsFile& file);
};

/**
 * Represents a large first letter (drop cap) at the start of a chapter or paragraph.
 */
class PageDropCap final : public PageElement {
  std::string text;
  int dropCapFontId;

 public:
  /**
   * @param text The character(s) to render as a drop cap
   * @param xPos X coordinate
   * @param yPos Y coordinate
   * @param fontId The specific large font ID to use
   */
  PageDropCap(std::string text, const int16_t xPos, const int16_t yPos, int fontId)
      : PageElement(xPos, yPos), text(std::move(text)), dropCapFontId(fontId) {}

  const std::string& getDropCapText() const { return text; }
  int getDropCapFontId() const { return dropCapFontId; }

  PageElementTag getTag() const override { return TAG_PageDropCap; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
              BitmapDitherMode imageDitherMode = BitmapDitherMode::None) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageDropCap> deserialize(FsFile& file);
};

/**
 * Represents an image on a page.
 * Stores the path to the cached BMP file and its dimensions.
 */
class PageImage final : public PageElement {
  std::string cachePath;
  int16_t width;
  int16_t height;

 public:
  PageImage(std::string path, const int16_t w, const int16_t h, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), cachePath(std::move(path)), width(w), height(h) {}

  PageElementTag getTag() const override { return TAG_PageImage; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
              BitmapDitherMode imageDitherMode = BitmapDitherMode::None) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageImage> deserialize(FsFile& file);
  
  const std::string& getPath() const { return cachePath; }
  int16_t getWidth() const { return width; }
  int16_t getHeight() const { return height; }
};

/**
 * Represents a complete page containing multiple elements.
 */
class Page {
 public:
  std::vector<std::shared_ptr<PageElement>> elements;
  
  bool hasImages() const {
    return std::any_of(elements.begin(), elements.end(),
                       [](const std::shared_ptr<PageElement>& element) {
                         return element->getTag() == TAG_PageImage;
                       });
  }

  /**
   * Union of all image paint rectangles in screen coordinates (tight fit from BMP dimensions and drawBitmap
   * scaling, matching PageImage::render). Used for partial clears (e.g. text AA prep on image pages).
   * @return false if there are no images.
   */
  bool getImageBoundingBox(const GfxRenderer& renderer, int xOffset, int yOffset, int16_t& outX, int16_t& outY,
                           int16_t& outW, int16_t& outH) const;

  void render(GfxRenderer& renderer, int fontId, int headerFontId, int xOffset, int yOffset, bool skipImages = false,
              BitmapDitherMode imageDitherMode = BitmapDitherMode::None) const;
  void renderImages(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
                    BitmapDitherMode imageDitherMode = BitmapDitherMode::None) const;
  bool serialize(FsFile& file) const;
  static std::unique_ptr<Page> deserialize(FsFile& file);
};