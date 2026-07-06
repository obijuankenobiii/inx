#pragma once

/**
 * @file Page.h
 * @brief Public interface and types for Page.
 */

#include <ImageRenderMode.h>
#include <SdFat.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "blocks/TextBlock.h"

class GfxRenderer;

enum PageElementTag : uint8_t {
  TAG_PageLine = 1,
  TAG_PageHeader = 2,
  TAG_PageImage = 3,
  TAG_PageDropCap = 4,
  TAG_PageTable = 5,
  TAG_PageHorizontalRule = 6,
  TAG_PageSmallCaps = 7,
  TAG_PageCssBorderLine = 8,
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
   * @param imageMode Image output depth for image elements.
   */
  virtual void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
                      ImageRenderMode imageMode = ImageRenderMode::OneBit) = 0;

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
              ImageRenderMode imageMode = ImageRenderMode::OneBit) override;
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
              ImageRenderMode imageMode = ImageRenderMode::OneBit) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageHeader> deserialize(FsFile& file);
};

/**
 * Represents a line of text containing small-caps words.
 * Small-caps now render from the active body font, but we keep the stored int for
 * serialized page compatibility with older cache files.
 */
class PageSmallCaps final : public PageElement {
  std::shared_ptr<TextBlock> block;
  int compatFontId;

 public:
  PageSmallCaps(std::shared_ptr<TextBlock> block, const int16_t xPos, const int16_t yPos, int fontId)
      : PageElement(xPos, yPos), block(std::move(block)), compatFontId(fontId) {}

  const TextBlock& getTextBlock() const { return *block; }
  int getCompatFontId() const { return compatFontId; }

  PageElementTag getTag() const override { return TAG_PageSmallCaps; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
              ImageRenderMode imageMode = ImageRenderMode::OneBit) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageSmallCaps> deserialize(FsFile& file);
};

/**
 * Represents a large first letter (drop cap) at the start of a chapter or paragraph.
 */
class PageDropCap final : public PageElement {
  std::string text;
  int dropCapFontId;
  bool inlineFirstLine;

 public:
  /**
   * @param text The character(s) to render as a drop cap
   * @param xPos X coordinate
   * @param yPos Y coordinate
   * @param fontId The specific large font ID to use
   */
  PageDropCap(std::string text, const int16_t xPos, const int16_t yPos, int fontId, bool inlineFirstLine = false)
      : PageElement(xPos, yPos), text(std::move(text)), dropCapFontId(fontId), inlineFirstLine(inlineFirstLine) {}

  const std::string& getDropCapText() const { return text; }
  int getDropCapFontId() const { return dropCapFontId; }
  bool isInlineFirstLine() const { return inlineFirstLine; }
  static constexpr int16_t VERTICAL_ADJUSTMENT = 0;

  PageElementTag getTag() const override { return TAG_PageDropCap; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
              ImageRenderMode imageMode = ImageRenderMode::OneBit) override;
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
  bool grayscale;  // true = image has continuous-tone content worth grayscale; false = ~1-bit (comic/line art)

 public:
  PageImage(std::string path, const int16_t w, const int16_t h, const int16_t xPos, const int16_t yPos,
            const bool grayscale = true)
      : PageElement(xPos, yPos), cachePath(std::move(path)), width(w), height(h), grayscale(grayscale) {}

  PageElementTag getTag() const override { return TAG_PageImage; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
              ImageRenderMode imageMode = ImageRenderMode::OneBit) override;
  // Same as render() but lets the caller select the quality render path (options.quality).
  void renderImage(GfxRenderer& renderer, int fontId, int xOffset, int yOffset, ImageRenderMode imageMode,
                   bool quality);
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageImage> deserialize(FsFile& file);

  const std::string& getPath() const { return cachePath; }
  int16_t getWidth() const { return width; }
  int16_t getHeight() const { return height; }
  bool needsGrayscale() const { return grayscale; }
};

class PageTable final : public PageElement {
 public:
  struct Cell {
    bool header = false;
    uint16_t colspan = 1;
    std::vector<std::string> lines;
  };

 private:
  int16_t tableWidth;
  int16_t tableHeight;
  int16_t lineHeight;  ///< Effective per-text-line height (font line height * line spacing setting)
  bool showBorders;
  std::vector<uint16_t> columnWidths;
  std::vector<uint16_t> rowHeights;
  std::vector<std::vector<Cell>> rows;

 public:
  PageTable(std::vector<std::vector<Cell>> rows, std::vector<uint16_t> columnWidths, std::vector<uint16_t> rowHeights,
            const bool showBorders, const int16_t tableWidth, const int16_t tableHeight, const int16_t lineHeight,
            const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos),
        tableWidth(tableWidth),
        tableHeight(tableHeight),
        lineHeight(lineHeight),
        showBorders(showBorders),
        columnWidths(std::move(columnWidths)),
        rowHeights(std::move(rowHeights)),
        rows(std::move(rows)) {}

  PageElementTag getTag() const override { return TAG_PageTable; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
              ImageRenderMode imageMode = ImageRenderMode::OneBit) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageTable> deserialize(FsFile& file);

  int16_t getHeight() const { return tableHeight; }
};

class PageHorizontalRule final : public PageElement {
 public:
  static constexpr int16_t WIDTH = 180;
  static constexpr int16_t HEIGHT = 15;

  PageHorizontalRule(const int16_t xPos, const int16_t yPos) : PageElement(xPos, yPos) {}

  PageElementTag getTag() const override { return TAG_PageHorizontalRule; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
              ImageRenderMode imageMode = ImageRenderMode::OneBit) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageHorizontalRule> deserialize(FsFile& file);
};

class PageCssBorderLine final : public PageElement {
 public:
  /** CSS border-style rendering for the horizontal rule (maps to the CSS keywords). */
  enum Style : uint8_t { SOLID = 0, DOUBLE = 1, DOTTED = 2, DASHED = 3 };

 private:
  int16_t width;
  int16_t thickness;
  uint8_t style;

 public:
  PageCssBorderLine(const int16_t xPos, const int16_t yPos, const int16_t width, const int16_t thickness,
                    const uint8_t style = SOLID)
      : PageElement(xPos, yPos), width(width), thickness(thickness), style(style) {}

  PageElementTag getTag() const override { return TAG_PageCssBorderLine; }
  /** Sets the horizontal position/width after layout (used to size a deferred rule to the text width). */
  void setGeometry(const int16_t x, const int16_t w) {
    xPos = x;
    width = w;
  }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
              ImageRenderMode imageMode = ImageRenderMode::OneBit) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageCssBorderLine> deserialize(FsFile& file);
};

/**
 * Represents a complete page containing multiple elements.
 */
class Page {
 public:
  std::vector<std::shared_ptr<PageElement>> elements;

  bool hasImages() const {
    return std::any_of(elements.begin(), elements.end(),
                       [](const std::shared_ptr<PageElement>& element) { return element->getTag() == TAG_PageImage; });
  }

  // True if at least one image on the page has continuous-tone content worth rendering in grayscale. Pages whose
  // images are all essentially 1-bit (comics / line art / mostly black-and-white) return false, so they can be
  // rendered as fast 1-bit instead of paying for the grayscale passes.
  bool anyImageNeedsGrayscale() const;

  /**
   * Union of all image paint rectangles in screen coordinates (tight fit from BMP dimensions and drawBitmap
   * scaling, matching PageImage::render). Used for partial clears (e.g. text AA prep on image pages).
   * @return false if there are no images.
   */
  bool getImageBoundingBox(const GfxRenderer& renderer, int xOffset, int yOffset, int16_t& outX, int16_t& outY,
                           int16_t& outW, int16_t& outH) const;

  // Fills EACH image's own paint rectangle (centered, at its stored size) with `value` — NOT the union bounding
  // box. Use this for per-image baseline marks / GRAY2 white bases so text between images on the same page is
  // never covered. Matches PageImage::render geometry.
  void fillImageRects(GfxRenderer& renderer, int xOffset, int yOffset, bool value, bool onlyGrayscale = false) const;

  void render(GfxRenderer& renderer, int fontId, int headerFontId, int xOffset, int yOffset, bool skipImages = false,
              ImageRenderMode imageMode = ImageRenderMode::OneBit, bool skipOnlyGrayscaleImages = false) const;
  // `quality` routes images through the quality render path (options.quality=true) — the same path the sleep
  // screen uses — instead of the default 1-bit/medium path.
  void renderImages(GfxRenderer& renderer, int fontId, int xOffset, int yOffset,
                    ImageRenderMode imageMode = ImageRenderMode::OneBit, bool quality = false,
                    bool onlyGrayscale = false) const;
  bool serialize(FsFile& file) const;
  static std::unique_ptr<Page> deserialize(FsFile& file);
};
