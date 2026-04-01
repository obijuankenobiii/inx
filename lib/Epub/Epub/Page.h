#pragma once
#include <SdFat.h>
#include <utility>
#include <vector>
#include <algorithm>

#include "blocks/TextBlock.h"

enum PageElementTag : uint8_t {
  TAG_PageLine = 1,
  TAG_PageHeader = 2,
  TAG_PageImage = 3,
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
   * 
   * @param xPos X coordinate on the page
   * @param yPos Y coordinate on the page
   */
  explicit PageElement(const int16_t xPos, const int16_t yPos) : xPos(xPos), yPos(yPos) {}
  virtual ~PageElement() = default;
  
  /**
   * Returns the element type tag for identification.
   * 
   * @return The element type tag
   */
  virtual PageElementTag getTag() const = 0;
  
  /**
   * Renders the element on the screen.
   * 
   * @param renderer The graphics renderer
   * @param fontId Font ID for text rendering
   * @param xOffset Horizontal offset for page margins
   * @param yOffset Vertical offset for page margins
   */
  virtual void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) = 0;
  
  /**
   * Serializes the element to a file.
   * 
   * @param file The file to write to
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
  /**
   * Constructs a new page line.
   * 
   * @param block The text block containing the line's content
   * @param xPos X coordinate on the page
   * @param yPos Y coordinate on the page
   */
  PageLine(std::shared_ptr<TextBlock> block, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), block(std::move(block)) {}

  PageElementTag getTag() const override { return TAG_PageLine; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageLine> deserialize(FsFile& file);
};

/**
 * Represents a header line on a page.
 * Uses the next larger font size from FontManager.
 */
class PageHeader final : public PageElement {
  std::shared_ptr<TextBlock> block;
  int headerFontId;  // Add this line

 public:
  /**
   * Constructs a new page header.
   * 
   * @param block The text block containing the header's content
   * @param xPos X coordinate on the page
   * @param yPos Y coordinate on the page
   * @param fontId The font ID to use for this header
   */
  PageHeader(std::shared_ptr<TextBlock> block, const int16_t xPos, const int16_t yPos, int fontId)
      : PageElement(xPos, yPos), block(std::move(block)), headerFontId(fontId) {}

  PageElementTag getTag() const override { return TAG_PageHeader; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageHeader> deserialize(FsFile& file);
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
  /**
   * Constructs a new page image.
   * 
   * @param path Path to the cached BMP file
   * @param w Width of the image
   * @param h Height of the image
   * @param xPos X coordinate on the page
   * @param yPos Y coordinate on the page
   */
  PageImage(std::string path, const int16_t w, const int16_t h, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), cachePath(std::move(path)), width(w), height(h) {}

  PageElementTag getTag() const override { return TAG_PageImage; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) override;
  bool serialize(FsFile& file) override;
  static std::unique_ptr<PageImage> deserialize(FsFile& file);
  
  /**
   * Gets the path to the cached BMP file.
   * 
   * @return The cache path
   */
  const std::string& getPath() const { return cachePath; }
  
  /**
   * Gets the image width.
   * 
   * @return The width in pixels
   */
  int16_t getWidth() const { return width; }
  
  /**
   * Gets the image height.
   * 
   * @return The height in pixels
   */
  int16_t getHeight() const { return height; }
};

/**
 * Represents a complete page containing multiple elements.
 * Manages a collection of page elements and handles rendering and serialization.
 */
class Page {
 public:
  std::vector<std::shared_ptr<PageElement>> elements;
  
  /**
   * Checks if the page contains any images.
   * 
   * @return true if at least one image element exists
   */
  bool hasImages() const {
    return std::any_of(elements.begin(), elements.end(),
                       [](const std::shared_ptr<PageElement>& element) {
                         return element->getTag() == TAG_PageImage;
                       });
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
  void render(GfxRenderer& renderer, int fontId, int headerFontId, int xOffset, int yOffset, bool skipImages = false) const;
  
  /**
   * Serializes the page to a file.
   * 
   * @param file The file to write to
   * @return true if serialization was successful
   */
  bool serialize(FsFile& file) const;
  
  /**
   * Deserializes a page from a file.
   * 
   * @param file The file to read from
   * @return Unique pointer to the deserialized page
   */
  static std::unique_ptr<Page> deserialize(FsFile& file);
};