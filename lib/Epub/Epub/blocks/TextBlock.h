#pragma once

/**
 * @file TextBlock.h
 * @brief Public interface and types for TextBlock.
 */

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
  std::list<uint8_t> bionicPrefixBytes;
  std::list<uint8_t> wordSmallCaps;
  std::list<uint8_t> wordUnderline;
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
                     std::list<EpdFontFamily::Style> word_styles, std::list<uint8_t> word_small_caps,
                     const Style style, std::list<uint8_t> word_underline = {})
      : words(std::move(words)), wordXpos(std::move(word_xpos)),
        wordStyles(std::move(word_styles)), wordSmallCaps(std::move(word_small_caps)),
        wordUnderline(std::move(word_underline)), style(style) {}

  explicit TextBlock(std::list<std::string> words, std::list<uint16_t> word_xpos,
                     std::list<EpdFontFamily::Style> word_styles, std::list<uint8_t> bionic_prefix_bytes,
                     std::list<uint8_t> word_small_caps,
                     const Style style, std::list<uint8_t> word_underline = {})
      : words(std::move(words)),
        wordXpos(std::move(word_xpos)),
        wordStyles(std::move(word_styles)),
        bionicPrefixBytes(std::move(bionic_prefix_bytes)),
        wordSmallCaps(std::move(word_small_caps)),
        wordUnderline(std::move(word_underline)),
        style(style) {}
  
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

  size_t getWordCount() const { return words.size(); }
  /** True if any word in the line is flagged small caps. */
  bool hasSmallCaps() const {
    for (const auto f : wordSmallCaps) {
      if (f != 0) return true;
    }
    return false;
  }
  std::string getWordAt(size_t index) const;
  uint16_t getWordXAt(size_t index) const;
  EpdFontFamily::Style getWordStyleAt(size_t index) const;
  
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
  void render(const GfxRenderer& renderer, int fontId, int smallCapsFontId, int x, int y) const;
  void prewarm(const GfxRenderer& renderer, int fontId, int smallCapsFontId) const;
  
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
