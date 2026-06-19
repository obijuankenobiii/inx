/**
 * @file TextBlock.cpp
 * @brief Definitions for TextBlock.
 */

#include "TextBlock.h"

#include <GfxRenderer.h>
#include <Serialization.h>
#include <Utf8.h>

#include <algorithm>
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

bool isAsciiLower(const uint32_t cp) { return cp >= 'a' && cp <= 'z'; }

uint32_t toAsciiUpper(const uint32_t cp) { return isAsciiLower(cp) ? (cp - ('a' - 'A')) : cp; }

void appendUtf8Codepoint(std::string& out, const uint32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

int renderSmallCapsSegment(const GfxRenderer& renderer, const int fontId, int x, const int y, const std::string& text,
                           const EpdFontFamily::Style style) {
  if (text.empty()) {
    return x;
  }

  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text.c_str());
  std::string upper;

  while (const uint32_t cp = utf8NextCodepoint(&ptr)) {
    appendUtf8Codepoint(upper, toAsciiUpper(cp));
  }
  renderer.text.render(fontId, x, y, upper.c_str(), true, style);
  x += renderer.text.getWidth(fontId, upper.c_str(), style);
  return x;
}

void prewarmSmallCapsSegment(const GfxRenderer& renderer, const int fontId, const std::string& text,
                             const EpdFontFamily::Style style) {
  if (text.empty()) {
    return;
  }
  std::string upper;
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&ptr)) {
    appendUtf8Codepoint(upper, toAsciiUpper(cp));
  }
  renderer.text.prewarm(fontId, upper.c_str(), style);
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

void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      (!bionicPrefixBytes.empty() && bionicPrefixBytes.size() != words.size()) ||
      (!wordSmallCaps.empty() && wordSmallCaps.size() != words.size())) {
    Serial.printf("[%lu] [TXB] Render skipped: size mismatch (words=%u, xpos=%u, styles=%u, bionic=%u, sc=%u)\n",
                  millis(), (uint32_t)words.size(), (uint32_t)wordXpos.size(), (uint32_t)wordStyles.size(),
                  (uint32_t)bionicPrefixBytes.size(), (uint32_t)wordSmallCaps.size());
    return;
  }

  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  auto xIt = wordXpos.begin();
  auto prefixIt = bionicPrefixBytes.begin();
  auto smallCapsIt = wordSmallCaps.begin();

  for (; wordIt != words.end() && styleIt != wordStyles.end() && xIt != wordXpos.end(); ++wordIt, ++styleIt, ++xIt) {
    const uint8_t prefixBytes = bionicPrefixBytes.empty() ? 0 : *prefixIt;
    const bool smallCaps = !wordSmallCaps.empty() && (*smallCapsIt != 0);
    if (prefixBytes == 0 || prefixBytes >= wordIt->size()) {
      if (smallCaps) {
        renderSmallCapsSegment(renderer, fontId, *xIt + x, y, *wordIt, *styleIt);
      } else {
        renderer.text.render(fontId, *xIt + x, y, wordIt->c_str(), true, *styleIt);
      }
    } else {
      const std::string prefix = wordIt->substr(0, prefixBytes);
      const std::string suffix = wordIt->substr(prefixBytes);
      const auto prefixStyle = bionicStyleFor(*styleIt);
      if (smallCaps) {
        const int suffixX = renderSmallCapsSegment(renderer, fontId, *xIt + x, y, prefix, prefixStyle);
        renderSmallCapsSegment(renderer, fontId, suffixX, y, suffix, *styleIt);
      } else {
        renderer.text.render(fontId, *xIt + x, y, prefix.c_str(), true, prefixStyle);
        const int suffixX = *xIt + x + renderer.text.getWidth(fontId, prefix.c_str(), prefixStyle);
        renderer.text.render(fontId, suffixX, y, suffix.c_str(), true, *styleIt);
      }
    }
    if (!bionicPrefixBytes.empty()) {
      ++prefixIt;
    }
    if (!wordSmallCaps.empty()) {
      ++smallCapsIt;
    }
  }
}

void TextBlock::prewarm(const GfxRenderer& renderer, const int fontId) const {
  if (words.size() != wordStyles.size() || (!wordSmallCaps.empty() && wordSmallCaps.size() != words.size())) {
    return;
  }

  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  auto prefixIt = bionicPrefixBytes.begin();
  auto smallCapsIt = wordSmallCaps.begin();
  for (; wordIt != words.end() && styleIt != wordStyles.end(); ++wordIt, ++styleIt) {
    const uint8_t prefixBytes = bionicPrefixBytes.empty() ? 0 : *prefixIt;
    const bool smallCaps = !wordSmallCaps.empty() && (*smallCapsIt != 0);
    if (prefixBytes == 0 || prefixBytes >= wordIt->size()) {
      if (smallCaps) {
        prewarmSmallCapsSegment(renderer, fontId, *wordIt, *styleIt);
      } else {
        renderer.text.prewarm(fontId, wordIt->c_str(), *styleIt);
      }
    } else {
      const std::string prefix = wordIt->substr(0, prefixBytes);
      const std::string suffix = wordIt->substr(prefixBytes);
      const auto prefixStyle = bionicStyleFor(*styleIt);
      if (smallCaps) {
        prewarmSmallCapsSegment(renderer, fontId, prefix, prefixStyle);
        prewarmSmallCapsSegment(renderer, fontId, suffix, *styleIt);
      } else {
        renderer.text.prewarm(fontId, prefix.c_str(), prefixStyle);
        renderer.text.prewarm(fontId, suffix.c_str(), *styleIt);
      }
    }
    if (!bionicPrefixBytes.empty()) {
      ++prefixIt;
    }
    if (!wordSmallCaps.empty()) {
      ++smallCapsIt;
    }
  }
}

bool TextBlock::serialize(FsFile& file) const {
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      (!bionicPrefixBytes.empty() && bionicPrefixBytes.size() != words.size()) ||
      (!wordSmallCaps.empty() && wordSmallCaps.size() != words.size())) {
    Serial.printf("[%lu] [TXB] Serialization failed: size mismatch (words=%u, xpos=%u, styles=%u, bionic=%u, sc=%u)\n",
                  millis(), words.size(), wordXpos.size(), wordStyles.size(), bionicPrefixBytes.size(),
                  wordSmallCaps.size());
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
  if (wordSmallCaps.empty()) {
    for (size_t i = 0; i < words.size(); ++i) serialization::writePod(file, static_cast<uint8_t>(0));
  } else {
    for (auto f : wordSmallCaps) serialization::writePod(file, f);
  }
  serialization::writePod(file, style);

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(FsFile& file) {
  uint16_t wc;
  std::list<std::string> words;
  std::list<uint16_t> wordXpos;
  std::list<EpdFontFamily::Style> wordStyles;
  std::list<uint8_t> bionicPrefixBytes;
  std::list<uint8_t> wordSmallCaps;
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
  wordSmallCaps.resize(wc);
  for (auto& w : words) serialization::readString(file, w);
  for (auto& x : wordXpos) serialization::readPod(file, x);
  for (auto& s : wordStyles) serialization::readPod(file, s);
  for (auto& b : bionicPrefixBytes) serialization::readPod(file, b);
  for (auto& f : wordSmallCaps) serialization::readPod(file, f);
  serialization::readPod(file, style);

  return std::unique_ptr<TextBlock>(new TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles),
                                                  std::move(bionicPrefixBytes), std::move(wordSmallCaps), style));
}
