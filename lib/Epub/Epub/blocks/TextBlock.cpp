/**
 * @file TextBlock.cpp
 * @brief Definitions for TextBlock.
 */

#include "TextBlock.h"

#include <GfxRenderer.h>
#include <ImageRender.h>
#include <Serialization.h>
#include <Utf8.h>

#include <algorithm>
#include <iterator>
#include <string>

namespace {

constexpr uint8_t kScriptScalePct = 70;

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

int renderSmallCapsSegment(const GfxRenderer& renderer, const int fontId, const int x, const int y,
                           const std::string& text, const EpdFontFamily::Style style) {
  if (text.empty()) {
    return x;
  }
  // renderSmallCaps returns its advance, so no separate width measurement pass is needed.
  return renderer.text.renderSmallCaps(fontId, x, y, text.c_str(), true, style);
}

int renderWordSegment(const GfxRenderer& renderer, const int fontId, const int x, const int y, const std::string& text,
                      const EpdFontFamily::Style style, const bool smallCaps, const uint8_t verticalAlign) {
  if (text.empty()) {
    return x;
  }
  if (verticalAlign == TextBlock::SUPERSCRIPT || verticalAlign == TextBlock::SUBSCRIPT) {
    const int lineHeight = renderer.text.getLineHeight(fontId);
    const int scriptY =
        verticalAlign == TextBlock::SUPERSCRIPT ? y - std::max(1, lineHeight / 3) : y + std::max(1, lineHeight / 5);
    if (smallCaps) {
      return renderer.text.renderScaled(fontId, x, scriptY, text.c_str(), kScriptScalePct, true, style);
    }
    return renderer.text.renderScaled(fontId, x, scriptY, text.c_str(), kScriptScalePct, true, style);
  }
  if (smallCaps) {
    return renderSmallCapsSegment(renderer, fontId, x, y, text, style);
  }
  renderer.text.render(fontId, x, y, text.c_str(), true, style);
  return x + renderer.text.getWidth(fontId, text.c_str(), style);
}

int measureWordSegment(const GfxRenderer& renderer, const int fontId, const std::string& text,
                       const EpdFontFamily::Style style, const bool smallCaps, const uint8_t verticalAlign) {
  if (verticalAlign == TextBlock::SUPERSCRIPT || verticalAlign == TextBlock::SUBSCRIPT) {
    return renderer.text.getScaledWidth(fontId, text.c_str(), kScriptScalePct, style);
  }
  return smallCaps ? renderer.text.getSmallCapsWidth(fontId, text.c_str(), style)
                   : renderer.text.getWidth(fontId, text.c_str(), style);
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

void TextBlock::render(GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      (!bionicPrefixBytes.empty() && bionicPrefixBytes.size() != words.size()) ||
      (!wordSmallCaps.empty() && wordSmallCaps.size() != words.size()) ||
      (!wordVerticalAlign.empty() && wordVerticalAlign.size() != words.size()) ||
      (!wordImagePaths.empty() && wordImagePaths.size() != words.size())) {
    Serial.printf("[%lu] [TXB] Render skipped: size mismatch (words=%u, xpos=%u, styles=%u, bionic=%u, sc=%u, va=%u)\n",
                  millis(), (uint32_t)words.size(), (uint32_t)wordXpos.size(), (uint32_t)wordStyles.size(),
                  (uint32_t)bionicPrefixBytes.size(), (uint32_t)wordSmallCaps.size(),
                  (uint32_t)wordVerticalAlign.size());
    return;
  }

  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  auto xIt = wordXpos.begin();
  auto prefixIt = bionicPrefixBytes.begin();
  auto smallCapsIt = wordSmallCaps.begin();
  auto underlineIt = wordUnderline.begin();
  auto verticalAlignIt = wordVerticalAlign.begin();
  auto imgPathIt = wordImagePaths.begin();
  auto imgWIt = wordImageW.begin();
  auto imgHIt = wordImageH.begin();
  const bool hasImages = !wordImagePaths.empty();

  // Underline sits just below the baseline.
  const int underlineY = y + renderer.text.getFontAscenderSize(fontId) + 1;
  const int lineHeight = renderer.text.getLineHeight(fontId);

  for (; wordIt != words.end() && styleIt != wordStyles.end() && xIt != wordXpos.end(); ++wordIt, ++styleIt, ++xIt) {
    const uint8_t prefixBytes = bionicPrefixBytes.empty() ? 0 : *prefixIt;
    const bool smallCaps = !wordSmallCaps.empty() && (*smallCapsIt != 0);
    const bool underline = !wordUnderline.empty() && (*underlineIt != 0);
    const uint8_t verticalAlign = wordVerticalAlign.empty() ? BASELINE : *verticalAlignIt;
    const int startX = *xIt + x;
    int endX = startX;

    // Inline image word: draw the cached image vertically centered on the line and skip the text path.
    if (hasImages && imgPathIt != wordImagePaths.end() && !imgPathIt->empty()) {
      const int imgW = (imgWIt != wordImageW.end()) ? *imgWIt : 0;
      const int imgH = (imgHIt != wordImageH.end()) ? *imgHIt : 0;
      if (imgW > 0 && imgH > 0) {
        const int imgY = y + std::max(0, (lineHeight - imgH) / 2);
        ImageRender::create(renderer, *imgPathIt).render(startX, imgY, imgW, imgH, ImageRenderMode::OneBit);
      }
      if (!bionicPrefixBytes.empty()) ++prefixIt;
      if (!wordSmallCaps.empty()) ++smallCapsIt;
      if (!wordUnderline.empty()) ++underlineIt;
      if (!wordVerticalAlign.empty()) ++verticalAlignIt;
      ++imgPathIt;
      if (imgWIt != wordImageW.end()) ++imgWIt;
      if (imgHIt != wordImageH.end()) ++imgHIt;
      continue;
    }

    if (prefixBytes == 0 || prefixBytes >= wordIt->size()) {
      endX = renderWordSegment(renderer, fontId, startX, y, *wordIt, *styleIt, smallCaps, verticalAlign);
    } else {
      const std::string prefix = wordIt->substr(0, prefixBytes);
      const std::string suffix = wordIt->substr(prefixBytes);
      const auto prefixStyle = bionicStyleFor(*styleIt);
      const int suffixX = renderWordSegment(renderer, fontId, startX, y, prefix, prefixStyle, smallCaps, verticalAlign);
      endX = renderWordSegment(renderer, fontId, suffixX, y, suffix, *styleIt, smallCaps, verticalAlign);
    }
    if (underline && endX <= startX) {
      endX = startX + measureWordSegment(renderer, fontId, *wordIt, *styleIt, smallCaps, verticalAlign);
    }
    if (underline && endX > startX) {
      renderer.line.render(startX, underlineY, endX - 1, underlineY, true);
    }
    if (!bionicPrefixBytes.empty()) {
      ++prefixIt;
    }
    if (!wordSmallCaps.empty()) {
      ++smallCapsIt;
    }
    if (!wordUnderline.empty()) {
      ++underlineIt;
    }
    if (!wordVerticalAlign.empty()) {
      ++verticalAlignIt;
    }
    if (hasImages) {
      if (imgPathIt != wordImagePaths.end()) ++imgPathIt;
      if (imgWIt != wordImageW.end()) ++imgWIt;
      if (imgHIt != wordImageH.end()) ++imgHIt;
    }
  }
}

bool TextBlock::serialize(FsFile& file) const {
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      (!bionicPrefixBytes.empty() && bionicPrefixBytes.size() != words.size()) ||
      (!wordSmallCaps.empty() && wordSmallCaps.size() != words.size()) ||
      (!wordVerticalAlign.empty() && wordVerticalAlign.size() != words.size())) {
    Serial.printf(
        "[%lu] [TXB] Serialization failed: size mismatch (words=%u, xpos=%u, styles=%u, bionic=%u, sc=%u, va=%u)\n",
        millis(), words.size(), wordXpos.size(), wordStyles.size(), bionicPrefixBytes.size(), wordSmallCaps.size(),
        wordVerticalAlign.size());
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
  if (wordUnderline.empty()) {
    for (size_t i = 0; i < words.size(); ++i) serialization::writePod(file, static_cast<uint8_t>(0));
  } else {
    for (auto f : wordUnderline) serialization::writePod(file, f);
  }
  if (wordVerticalAlign.empty()) {
    for (size_t i = 0; i < words.size(); ++i) serialization::writePod(file, static_cast<uint8_t>(BASELINE));
  } else {
    for (auto f : wordVerticalAlign) serialization::writePod(file, f);
  }
  // Inline image fields (path + display size), parallel to words. A single flag keeps text-only lines free of
  // any per-word image data on disk (and avoids allocating empty placeholders on load).
  const uint8_t hasImages = wordImagePaths.empty() ? 0 : 1;
  serialization::writePod(file, hasImages);
  if (hasImages) {
    for (const auto& p : wordImagePaths) serialization::writeString(file, p);
    for (auto v : wordImageW) serialization::writePod(file, v);
    for (auto v : wordImageH) serialization::writePod(file, v);
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
  std::list<uint8_t> wordUnderline;
  std::list<uint8_t> wordVerticalAlign;
  std::list<std::string> wordImagePaths;
  std::list<uint16_t> wordImageW;
  std::list<uint16_t> wordImageH;
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
  wordUnderline.resize(wc);
  wordVerticalAlign.resize(wc);
  for (auto& w : words) serialization::readString(file, w);
  for (auto& x : wordXpos) serialization::readPod(file, x);
  for (auto& s : wordStyles) serialization::readPod(file, s);
  for (auto& b : bionicPrefixBytes) serialization::readPod(file, b);
  for (auto& f : wordSmallCaps) serialization::readPod(file, f);
  for (auto& f : wordUnderline) serialization::readPod(file, f);
  for (auto& f : wordVerticalAlign) serialization::readPod(file, f);
  uint8_t hasImages = 0;
  serialization::readPod(file, hasImages);
  if (hasImages) {
    wordImagePaths.resize(wc);
    wordImageW.resize(wc);
    wordImageH.resize(wc);
    for (auto& p : wordImagePaths) serialization::readString(file, p);
    for (auto& v : wordImageW) serialization::readPod(file, v);
    for (auto& v : wordImageH) serialization::readPod(file, v);
  }
  serialization::readPod(file, style);

  return std::unique_ptr<TextBlock>(
      new TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles), std::move(bionicPrefixBytes),
                    std::move(wordSmallCaps), style, std::move(wordUnderline), std::move(wordVerticalAlign),
                    std::move(wordImagePaths), std::move(wordImageW), std::move(wordImageH)));
}
