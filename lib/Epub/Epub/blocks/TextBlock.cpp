/**
 * @file TextBlock.cpp
 * @brief Definitions for TextBlock.
 */

#include "TextBlock.h"

#include <GfxRenderer.h>
#include <Serialization.h>

#include <iterator>
#include <string>

namespace {

EpdFontFamily::Style bionicStyleFor(EpdFontFamily::Style style) {
  switch (style) {
    case EpdFontFamily::ITALIC:
      return EpdFontFamily::BOLD_ITALIC;
    case EpdFontFamily::REGULAR:
      return EpdFontFamily::BOLD;
    case EpdFontFamily::BOLD:
    case EpdFontFamily::BOLD_ITALIC:
    default:
      return style;
  }
}

}  // namespace

std::string TextBlock::getWordAt(size_t index) const {
  if (index >= words.size()) return {};
  auto it = words.begin();
  std::advance(it, static_cast<std::ptrdiff_t>(index));
  return *it;
}

uint16_t TextBlock::getWordXAt(size_t index) const {
  if (index >= wordXpos.size()) return 0;
  auto it = wordXpos.begin();
  std::advance(it, static_cast<std::ptrdiff_t>(index));
  return *it;
}

EpdFontFamily::Style TextBlock::getWordStyleAt(size_t index) const {
  if (index >= wordStyles.size()) return EpdFontFamily::REGULAR;
  auto it = wordStyles.begin();
  std::advance(it, static_cast<std::ptrdiff_t>(index));
  return *it;
}

/**
 * Renders the text block at the specified position.
 * 
 * @param renderer The graphics renderer
 * @param fontId Font ID to use for rendering
 * @param x Base X coordinate
 * @param y Base Y coordinate
 */
void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      (!bionicPrefixBytes.empty() && bionicPrefixBytes.size() != words.size())) {
    Serial.printf("[%lu] [TXB] Render skipped: size mismatch (words=%u, xpos=%u, styles=%u, bionic=%u)\n", millis(),
                  (uint32_t)words.size(), (uint32_t)wordXpos.size(), (uint32_t)wordStyles.size(),
                  (uint32_t)bionicPrefixBytes.size());
    return;
  }

  auto wordIt = words.begin();
  auto wordStylesIt = wordStyles.begin();
  auto wordXposIt = wordXpos.begin();
  auto prefixIt = bionicPrefixBytes.begin();

  for (size_t i = 0; i < words.size(); i++) {
    const uint8_t prefixBytes = bionicPrefixBytes.empty() ? 0 : *prefixIt;
    if (prefixBytes == 0 || prefixBytes >= wordIt->size()) {
      renderer.text.render(fontId, *wordXposIt + x, y, wordIt->c_str(), true, *wordStylesIt);
    } else {
      const std::string prefix = wordIt->substr(0, prefixBytes);
      const std::string suffix = wordIt->substr(prefixBytes);
      const auto prefixStyle = bionicStyleFor(*wordStylesIt);
      renderer.text.render(fontId, *wordXposIt + x, y, prefix.c_str(), true, prefixStyle);
      const int suffixX = *wordXposIt + x + renderer.text.getWidth(fontId, prefix.c_str(), prefixStyle);
      renderer.text.render(fontId, suffixX, y, suffix.c_str(), true, *wordStylesIt);
    }

    std::advance(wordIt, 1);
    std::advance(wordStylesIt, 1);
    std::advance(wordXposIt, 1);
    if (!bionicPrefixBytes.empty()) {
      std::advance(prefixIt, 1);
    }
  }
}

void TextBlock::prewarm(const GfxRenderer& renderer, const int fontId) const {
  if (words.size() != wordStyles.size()) {
    return;
  }

  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  auto prefixIt = bionicPrefixBytes.begin();
  for (; wordIt != words.end() && styleIt != wordStyles.end(); ++wordIt, ++styleIt) {
    const uint8_t prefixBytes = bionicPrefixBytes.empty() ? 0 : *prefixIt;
    if (prefixBytes == 0 || prefixBytes >= wordIt->size()) {
      renderer.text.prewarm(fontId, wordIt->c_str(), *styleIt);
    } else {
      const std::string prefix = wordIt->substr(0, prefixBytes);
      const std::string suffix = wordIt->substr(prefixBytes);
      renderer.text.prewarm(fontId, prefix.c_str(), bionicStyleFor(*styleIt));
      renderer.text.prewarm(fontId, suffix.c_str(), *styleIt);
    }
    if (!bionicPrefixBytes.empty()) {
      ++prefixIt;
    }
  }
}

/**
 * Serializes the text block to a file.
 * 
 * @param file File to write to
 * @return true if successful
 */
bool TextBlock::serialize(FsFile& file) const {
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      (!bionicPrefixBytes.empty() && bionicPrefixBytes.size() != words.size())) {
    Serial.printf("[%lu] [TXB] Serialization failed: size mismatch (words=%u, xpos=%u, styles=%u, bionic=%u)\n",
                  millis(), words.size(), wordXpos.size(), wordStyles.size(), bionicPrefixBytes.size());
    return false;
  }

  
  serialization::writePod(file, static_cast<uint16_t>(words.size()));
  for (const auto& w : words) serialization::writeString(file, w);
  for (auto x : wordXpos) serialization::writePod(file, x);
  for (auto s : wordStyles) serialization::writePod(file, s);
  if (bionicPrefixBytes.empty()) {
    for (size_t i = 0; i < words.size(); ++i) serialization::writePod(file, static_cast<uint8_t>(0));
  } else {
    for (auto b : bionicPrefixBytes) serialization::writePod(file, b);
  }

  
  serialization::writePod(file, style);

  return true;
}

/**
 * Deserializes a text block from a file.
 * 
 * @param file File to read from
 * @return Unique pointer to the deserialized text block
 */
std::unique_ptr<TextBlock> TextBlock::deserialize(FsFile& file) {
  uint16_t wc;
  std::list<std::string> words;
  std::list<uint16_t> wordXpos;
  std::list<EpdFontFamily::Style> wordStyles;
  std::list<uint8_t> bionicPrefixBytes;
  Style style;

  
  serialization::readPod(file, wc);

  
  if (wc > 10000) {
    Serial.printf("[%lu] [TXB] Deserialization failed: word count %u exceeds maximum\n", millis(), wc);
    return nullptr;
  }

  
  words.resize(wc);
  wordXpos.resize(wc);
  wordStyles.resize(wc);
  bionicPrefixBytes.resize(wc);
  for (auto& w : words) serialization::readString(file, w);
  for (auto& x : wordXpos) serialization::readPod(file, x);
  for (auto& s : wordStyles) serialization::readPod(file, s);
  for (auto& b : bionicPrefixBytes) serialization::readPod(file, b);

  
  serialization::readPod(file, style);

  return std::unique_ptr<TextBlock>(new TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles),
                                                  std::move(bionicPrefixBytes), style));
}
