#pragma once
#include <EpdFontFamily.h>
#include <SdFat.h>

#include <list>
#include <memory>
#include <string>

#include "Block.h"

/**
 * Represents a line of text on a page.
 * Contains words, their positions, and styling information.
 */
class TextBlock final : public Block {
 public:
  enum Style : uint8_t {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
  };

 private:
  std::list<std::string> words;
  std::list<uint16_t> wordXpos;
  std::list<EpdFontFamily::Style> wordStyles;
  Style style;

 public:
  /**
   * Constructs a new TextBlock.
   * 
   * @param words List of words in the line
   * @param word_xpos X positions for each word
   * @param word_styles Font styles for each word
   * @param style Alignment style for the line
   */
  explicit TextBlock(std::list<std::string> words, std::list<uint16_t> word_xpos,
                     std::list<EpdFontFamily::Style> word_styles, const Style style)
      : words(std::move(words)), wordXpos(std::move(word_xpos)), 
        wordStyles(std::move(word_styles)), style(style) {}
  
  ~TextBlock() override = default;
  
  /**
   * Sets the alignment style.
   * 
   * @param style New alignment style
   */
  void setStyle(const Style style) { this->style = style; }
  
  /**
   * Gets the current alignment style.
   * 
   * @return Current style
   */
  Style getStyle() const { return style; }
  
  /**
   * Checks if the block contains any words.
   * 
   * @return true if empty
   */
  bool isEmpty() override { return words.empty(); }
  
  /**
   * Layout is pre-calculated during parsing.
   */
  void layout(GfxRenderer& renderer) override {};
  
  /**
   * Renders the text block at the specified position.
   * 
   * @param renderer The graphics renderer
   * @param fontId Font ID to use for rendering
   * @param x Base X coordinate
   * @param y Base Y coordinate
   * @param spacingMultiplier Optional multiplier for word spacing (default 1.0)
   */
  void render(const GfxRenderer& renderer, int fontId, int x, int y) const;
  
  /**
   * Gets the block type identifier.
   * 
   * @return TEXT_BLOCK
   */
  BlockType getType() override { return TEXT_BLOCK; }
  
  /**
   * Serializes the text block to a file.
   * 
   * @param file File to write to
   * @return true if successful
   */
  bool serialize(FsFile& file) const;
  
  /**
   * Deserializes a text block from a file.
   * 
   * @param file File to read from
   * @return Unique pointer to the deserialized text block
   */
  static std::unique_ptr<TextBlock> deserialize(FsFile& file);
};