#pragma once

/**
 * @file TextBlock.h
 * @brief Public interface and types for TextBlock.
 */

#include <EpdFontFamily.h>
#include <SdFat.h>

#include <algorithm>
#include <iterator>
#include <list>
#include <memory>
#include <string>
#include <vector>

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
  enum VerticalAlign : uint8_t {
    BASELINE = 0,
    SUPERSCRIPT = 1,
    SUBSCRIPT = 2,
  };

 private:
  template <typename T>
  static std::vector<T> moveListToVector(std::list<T>& values) {
    std::vector<T> out;
    out.reserve(values.size());
    out.insert(out.end(), std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
    return out;
  }

  struct Extra {
    std::vector<EpdFontFamily::Style> wordStyles;
    std::vector<uint8_t> bionicPrefixBytes;
    std::vector<uint8_t> wordSmallCaps;
    std::vector<uint8_t> wordUnderline;
    std::vector<uint8_t> wordVerticalAlign;
    // Inline image "words": for an image slot the matching `words` entry is empty and these hold the cached
    // image path and its on-line display size. Empty path / 0 size means a normal text word.
    std::vector<std::string> wordImagePaths;
    std::vector<uint16_t> wordImageW;
    std::vector<uint16_t> wordImageH;
    // Footnote/endnote link target for a word inside an `<a href="...">` span, encoded as
    // "<resolvedPath>#<fragmentId>" (empty path means same file). Empty string means the word is not a
    // footnote marker. Present only when the block contains at least one footnote-marker word.
    std::vector<std::string> wordFootnoteTargets;
  };

  struct WordSlot {
    std::string text;
    int16_t xpos;
  };

  void initExtra(std::list<uint8_t> bionic_prefix_bytes, std::list<uint8_t> word_small_caps,
                 std::list<uint8_t> word_underline, std::list<uint8_t> word_vertical_align,
                 std::list<std::string> word_image_paths, std::list<uint16_t> word_image_w,
                 std::list<uint16_t> word_image_h, std::list<std::string> word_footnote_targets);
  void initExtra(std::vector<uint8_t> bionic_prefix_bytes, std::vector<uint8_t> word_small_caps,
                 std::vector<uint8_t> word_underline, std::vector<uint8_t> word_vertical_align,
                 std::vector<std::string> word_image_paths, std::vector<uint16_t> word_image_w,
                 std::vector<uint16_t> word_image_h, std::vector<std::string> word_footnote_targets);
  Extra& ensureExtra();
  void initWordStyles(std::list<EpdFontFamily::Style>& values);
  void initWordStyles(std::vector<EpdFontFamily::Style>&& values);
  static uint8_t compactByteList(std::list<uint8_t>& values, uint8_t emptyDefault);
  static uint8_t compactByteVector(std::vector<uint8_t>& values, uint8_t emptyDefault);
  static std::vector<WordSlot> makeWordSlots(std::list<std::string>& words, std::list<int16_t>& word_xpos);
  static std::vector<WordSlot> makeWordSlots(std::vector<std::string>&& words, std::vector<int16_t>&& word_xpos);

  std::vector<WordSlot> wordSlots;
  uint8_t bionicPrefixDefault = 0;
  uint8_t smallCapsDefault = 0;
  uint8_t underlineDefault = 0;
  uint8_t verticalAlignDefault = BASELINE;
  std::unique_ptr<Extra> extra;
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
  explicit TextBlock(std::list<std::string> words, std::list<int16_t> word_xpos,
                     std::list<EpdFontFamily::Style> word_styles, std::list<uint8_t> word_small_caps, const Style style,
                     std::list<uint8_t> word_underline = {}, std::list<uint8_t> word_vertical_align = {})
      : wordSlots(makeWordSlots(words, word_xpos)),
        style(style) {
    initExtra({}, std::move(word_small_caps), std::move(word_underline), std::move(word_vertical_align), {}, {}, {},
              {});
    initWordStyles(word_styles);
  }

  explicit TextBlock(std::list<std::string> words, std::list<int16_t> word_xpos,
                     std::list<EpdFontFamily::Style> word_styles, std::list<uint8_t> bionic_prefix_bytes,
                     std::list<uint8_t> word_small_caps, const Style style, std::list<uint8_t> word_underline = {},
                     std::list<uint8_t> word_vertical_align = {}, std::list<std::string> word_image_paths = {},
                     std::list<uint16_t> word_image_w = {}, std::list<uint16_t> word_image_h = {},
                     std::list<std::string> word_footnote_targets = {})
      : wordSlots(makeWordSlots(words, word_xpos)),
        style(style) {
    initExtra(std::move(bionic_prefix_bytes), std::move(word_small_caps), std::move(word_underline),
              std::move(word_vertical_align), std::move(word_image_paths), std::move(word_image_w),
              std::move(word_image_h), std::move(word_footnote_targets));
    initWordStyles(word_styles);
  }

  explicit TextBlock(std::vector<std::string> words, std::vector<int16_t> word_xpos,
                     std::vector<EpdFontFamily::Style> word_styles, std::vector<uint8_t> bionic_prefix_bytes,
                     std::vector<uint8_t> word_small_caps, const Style style, std::vector<uint8_t> word_underline = {},
                     std::vector<uint8_t> word_vertical_align = {}, std::vector<std::string> word_image_paths = {},
                     std::vector<uint16_t> word_image_w = {}, std::vector<uint16_t> word_image_h = {},
                     std::vector<std::string> word_footnote_targets = {});

  explicit TextBlock(std::vector<std::string> words, std::vector<int16_t> word_xpos,
                     std::vector<EpdFontFamily::Style> word_styles, uint8_t bionic_prefix_default,
                     std::vector<uint8_t> bionic_prefix_bytes, uint8_t small_caps_default,
                     std::vector<uint8_t> word_small_caps, const Style style, uint8_t underline_default,
                     std::vector<uint8_t> word_underline, uint8_t vertical_align_default,
                     std::vector<uint8_t> word_vertical_align, std::vector<std::string> word_image_paths = {},
                     std::vector<uint16_t> word_image_w = {}, std::vector<uint16_t> word_image_h = {},
                     std::vector<std::string> word_footnote_targets = {});

  ~TextBlock() override = default;
  TextBlock(TextBlock&&) noexcept = default;
  TextBlock& operator=(TextBlock&&) noexcept = default;

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
  bool isEmpty() override { return wordSlots.empty(); }

  size_t getWordCount() const { return wordSlots.size(); }
  /** True if any word in the line is flagged small caps. */
  bool hasSmallCaps() const {
    return smallCapsDefault != 0 ||
           (extra && std::any_of(extra->wordSmallCaps.begin(), extra->wordSmallCaps.end(),
                                 [](uint8_t f) { return f != 0; }));
  }
  std::string getWordAt(size_t index) const;
  int16_t getWordXAt(size_t index) const;
  EpdFontFamily::Style getWordStyleAt(size_t index) const;

  /**
   * Single O(n) pass over the words. Avoids the O(n^2) indexed accessors (each std::advance walks the list)
   * and the per-word string copy when callers need every word's text, x position and style.
   * Callback signature: (size_t index, const std::string& word, int16_t xpos, EpdFontFamily::Style style,
   * const std::string& footnoteTarget).
   */
  template <typename Fn>
  void forEachWord(Fn&& fn) const {
    static const std::string kNoFootnoteTarget;
    const auto* wordStyles = extra && !extra->wordStyles.empty() ? &extra->wordStyles : nullptr;
    const auto* footnoteTargets = extra && !extra->wordFootnoteTargets.empty() ? &extra->wordFootnoteTargets : nullptr;
    for (size_t i = 0; i < wordSlots.size(); ++i) {
      const EpdFontFamily::Style style = wordStyles ? (*wordStyles)[i] : EpdFontFamily::REGULAR;
      const std::string& footnoteTarget = footnoteTargets ? (*footnoteTargets)[i] : kNoFootnoteTarget;
      fn(i, wordSlots[i].text, wordSlots[i].xpos, style, footnoteTarget);
    }
  }

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
  void render(GfxRenderer& renderer, int fontId, int x, int y, bool black = true) const;

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
