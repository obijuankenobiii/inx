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
                           const std::string& text, const EpdFontFamily::Style style, const bool black) {
  if (text.empty()) {
    return x;
  }
  // renderSmallCaps returns its advance, so no separate width measurement pass is needed.
  return renderer.text.renderSmallCaps(fontId, x, y, text.c_str(), black, style);
}

int renderWordSegment(const GfxRenderer& renderer, const int fontId, const int x, const int y, const std::string& text,
                      const EpdFontFamily::Style style, const bool smallCaps, const uint8_t verticalAlign,
                      const bool black) {
  if (text.empty()) {
    return x;
  }
  if (verticalAlign == TextBlock::SUPERSCRIPT || verticalAlign == TextBlock::SUBSCRIPT) {
    const int lineHeight = renderer.text.getLineHeight(fontId);
    const int scriptY =
        verticalAlign == TextBlock::SUPERSCRIPT ? y - std::max(1, lineHeight / 3) : y + std::max(1, lineHeight / 5);
    if (smallCaps) {
      return renderer.text.renderScaled(fontId, x, scriptY, text.c_str(), kScriptScalePct, black, style);
    }
    return renderer.text.renderScaled(fontId, x, scriptY, text.c_str(), kScriptScalePct, black, style);
  }
  if (smallCaps) {
    return renderSmallCapsSegment(renderer, fontId, x, y, text, style, black);
  }
  renderer.text.render(fontId, x, y, text.c_str(), black, style);
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

uint8_t TextBlock::compactByteList(std::list<uint8_t>& values, const uint8_t emptyDefault) {
  if (values.empty()) {
    return emptyDefault;
  }
  const uint8_t first = values.front();
  if (std::all_of(values.begin(), values.end(), [first](uint8_t value) { return value == first; })) {
    values.clear();
    return first;
  }
  return emptyDefault;
}

uint8_t TextBlock::compactByteVector(std::vector<uint8_t>& values, const uint8_t emptyDefault) {
  if (values.empty()) {
    return emptyDefault;
  }
  const uint8_t first = values.front();
  if (std::all_of(values.begin(), values.end(), [first](uint8_t value) { return value == first; })) {
    values.clear();
    return first;
  }
  return emptyDefault;
}

TextBlock::Extra& TextBlock::ensureExtra() {
  if (!extra) {
    extra.reset(new Extra());
  }
  return *extra;
}

void TextBlock::initWordStyles(std::list<EpdFontFamily::Style>& values) {
  if (values.empty() || std::all_of(values.begin(), values.end(),
                                    [](EpdFontFamily::Style style) { return style == EpdFontFamily::REGULAR; })) {
    values.clear();
    return;
  }
  ensureExtra().wordStyles = moveListToVector(values);
}

void TextBlock::initWordStyles(std::vector<EpdFontFamily::Style>&& values) {
  if (values.empty() || std::all_of(values.begin(), values.end(),
                                    [](EpdFontFamily::Style style) { return style == EpdFontFamily::REGULAR; })) {
    return;
  }
  ensureExtra().wordStyles = std::move(values);
}

std::vector<TextBlock::WordSlot> TextBlock::makeWordSlots(std::list<std::string>& words,
                                                          std::list<int16_t>& word_xpos) {
  const size_t count = std::min(words.size(), word_xpos.size());
  std::vector<WordSlot> out;
  out.reserve(count);
  auto wordIt = words.begin();
  auto xIt = word_xpos.begin();
  for (size_t i = 0; i < count; ++i, ++wordIt, ++xIt) {
    out.push_back(WordSlot{std::move(*wordIt), *xIt});
  }
  return out;
}

std::vector<TextBlock::WordSlot> TextBlock::makeWordSlots(std::vector<std::string>&& words,
                                                          std::vector<int16_t>&& word_xpos) {
  const size_t count = std::min(words.size(), word_xpos.size());
  std::vector<WordSlot> out;
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    out.push_back(WordSlot{std::move(words[i]), word_xpos[i]});
  }
  return out;
}

void TextBlock::initExtra(std::list<uint8_t> bionic_prefix_bytes, std::list<uint8_t> word_small_caps,
                          std::list<uint8_t> word_underline, std::list<uint8_t> word_vertical_align,
                          std::list<std::string> word_image_paths, std::list<uint16_t> word_image_w,
                          std::list<uint16_t> word_image_h, std::list<std::string> word_footnote_targets) {
  bionicPrefixDefault = compactByteList(bionic_prefix_bytes, 0);
  smallCapsDefault = compactByteList(word_small_caps, 0);
  underlineDefault = compactByteList(word_underline, 0);
  verticalAlignDefault = compactByteList(word_vertical_align, BASELINE);
  if (word_image_paths.empty()) {
    word_image_w.clear();
    word_image_h.clear();
  }

  if (bionic_prefix_bytes.empty() && word_small_caps.empty() && word_underline.empty() &&
      word_vertical_align.empty() && word_image_paths.empty() && word_footnote_targets.empty()) {
    return;
  }

  Extra& extraRef = ensureExtra();
  extraRef.bionicPrefixBytes = moveListToVector(bionic_prefix_bytes);
  extraRef.wordSmallCaps = moveListToVector(word_small_caps);
  extraRef.wordUnderline = moveListToVector(word_underline);
  extraRef.wordVerticalAlign = moveListToVector(word_vertical_align);
  extraRef.wordImagePaths = moveListToVector(word_image_paths);
  extraRef.wordImageW = moveListToVector(word_image_w);
  extraRef.wordImageH = moveListToVector(word_image_h);
  extraRef.wordFootnoteTargets = moveListToVector(word_footnote_targets);
}

void TextBlock::initExtra(std::vector<uint8_t> bionic_prefix_bytes, std::vector<uint8_t> word_small_caps,
                          std::vector<uint8_t> word_underline, std::vector<uint8_t> word_vertical_align,
                          std::vector<std::string> word_image_paths, std::vector<uint16_t> word_image_w,
                          std::vector<uint16_t> word_image_h, std::vector<std::string> word_footnote_targets) {
  bionicPrefixDefault = compactByteVector(bionic_prefix_bytes, 0);
  smallCapsDefault = compactByteVector(word_small_caps, 0);
  underlineDefault = compactByteVector(word_underline, 0);
  verticalAlignDefault = compactByteVector(word_vertical_align, BASELINE);
  if (word_image_paths.empty()) {
    word_image_w.clear();
    word_image_h.clear();
  }

  if (bionic_prefix_bytes.empty() && word_small_caps.empty() && word_underline.empty() &&
      word_vertical_align.empty() && word_image_paths.empty() && word_footnote_targets.empty()) {
    return;
  }

  Extra& extraRef = ensureExtra();
  extraRef.bionicPrefixBytes = std::move(bionic_prefix_bytes);
  extraRef.wordSmallCaps = std::move(word_small_caps);
  extraRef.wordUnderline = std::move(word_underline);
  extraRef.wordVerticalAlign = std::move(word_vertical_align);
  extraRef.wordImagePaths = std::move(word_image_paths);
  extraRef.wordImageW = std::move(word_image_w);
  extraRef.wordImageH = std::move(word_image_h);
  extraRef.wordFootnoteTargets = std::move(word_footnote_targets);
}

TextBlock::TextBlock(std::vector<std::string> words, std::vector<int16_t> word_xpos,
                     std::vector<EpdFontFamily::Style> word_styles, std::vector<uint8_t> bionic_prefix_bytes,
                     std::vector<uint8_t> word_small_caps, const Style style, std::vector<uint8_t> word_underline,
                     std::vector<uint8_t> word_vertical_align, std::vector<std::string> word_image_paths,
                     std::vector<uint16_t> word_image_w, std::vector<uint16_t> word_image_h,
                     std::vector<std::string> word_footnote_targets)
    : wordSlots(makeWordSlots(std::move(words), std::move(word_xpos))),
      style(style) {
  initExtra(std::move(bionic_prefix_bytes), std::move(word_small_caps), std::move(word_underline),
            std::move(word_vertical_align), std::move(word_image_paths), std::move(word_image_w),
            std::move(word_image_h), std::move(word_footnote_targets));
  initWordStyles(std::move(word_styles));
}

TextBlock::TextBlock(std::vector<std::string> words, std::vector<int16_t> word_xpos,
                     std::vector<EpdFontFamily::Style> word_styles, const uint8_t bionic_prefix_default,
                     std::vector<uint8_t> bionic_prefix_bytes, const uint8_t small_caps_default,
                     std::vector<uint8_t> word_small_caps, const Style style, const uint8_t underline_default,
                     std::vector<uint8_t> word_underline, const uint8_t vertical_align_default,
                     std::vector<uint8_t> word_vertical_align, std::vector<std::string> word_image_paths,
                     std::vector<uint16_t> word_image_w, std::vector<uint16_t> word_image_h,
                     std::vector<std::string> word_footnote_targets)
    : wordSlots(makeWordSlots(std::move(words), std::move(word_xpos))),
      bionicPrefixDefault(bionic_prefix_default),
      smallCapsDefault(small_caps_default),
      underlineDefault(underline_default),
      verticalAlignDefault(vertical_align_default),
      style(style) {
  if (word_image_paths.empty()) {
    word_image_w.clear();
    word_image_h.clear();
  }
  if (bionic_prefix_bytes.empty() && word_small_caps.empty() && word_underline.empty() &&
      word_vertical_align.empty() && word_image_paths.empty() && word_footnote_targets.empty()) {
    initWordStyles(std::move(word_styles));
    return;
  }
  Extra& extraRef = ensureExtra();
  extraRef.bionicPrefixBytes = std::move(bionic_prefix_bytes);
  extraRef.wordSmallCaps = std::move(word_small_caps);
  extraRef.wordUnderline = std::move(word_underline);
  extraRef.wordVerticalAlign = std::move(word_vertical_align);
  extraRef.wordImagePaths = std::move(word_image_paths);
  extraRef.wordImageW = std::move(word_image_w);
  extraRef.wordImageH = std::move(word_image_h);
  extraRef.wordFootnoteTargets = std::move(word_footnote_targets);
  initWordStyles(std::move(word_styles));
}

std::string TextBlock::getWordAt(size_t index) const {
  if (index >= wordSlots.size()) return {};
  return wordSlots[index].text;
}

int16_t TextBlock::getWordXAt(size_t index) const {
  if (index >= wordSlots.size()) return 0;
  return wordSlots[index].xpos;
}

EpdFontFamily::Style TextBlock::getWordStyleAt(size_t index) const {
  const auto* wordStyles = extra && !extra->wordStyles.empty() ? &extra->wordStyles : nullptr;
  if (!wordStyles || index >= wordStyles->size()) return EpdFontFamily::REGULAR;
  return (*wordStyles)[index];
}

void TextBlock::render(GfxRenderer& renderer, const int fontId, const int x, const int y, const bool black) const {
  const auto* wordStyles = extra && !extra->wordStyles.empty() ? &extra->wordStyles : nullptr;
  const auto* bionicPrefixBytes = extra ? &extra->bionicPrefixBytes : nullptr;
  const auto* wordSmallCaps = extra ? &extra->wordSmallCaps : nullptr;
  const auto* wordUnderline = extra ? &extra->wordUnderline : nullptr;
  const auto* wordVerticalAlign = extra ? &extra->wordVerticalAlign : nullptr;
  const auto* wordImagePaths = extra ? &extra->wordImagePaths : nullptr;
  const auto* wordImageW = extra ? &extra->wordImageW : nullptr;
  const auto* wordImageH = extra ? &extra->wordImageH : nullptr;

  const size_t wordCount = wordSlots.size();
  if ((wordStyles && wordCount != wordStyles->size()) ||
      (bionicPrefixBytes && !bionicPrefixBytes->empty() && bionicPrefixBytes->size() != wordCount) ||
      (wordSmallCaps && !wordSmallCaps->empty() && wordSmallCaps->size() != wordCount) ||
      (wordUnderline && !wordUnderline->empty() && wordUnderline->size() != wordCount) ||
      (wordVerticalAlign && !wordVerticalAlign->empty() && wordVerticalAlign->size() != wordCount) ||
      (wordImagePaths && !wordImagePaths->empty() && wordImagePaths->size() != wordCount)) {
    Serial.printf("[%lu] [TXB] Render skipped: size mismatch (words=%u, xpos=%u, styles=%u, bionic=%u, sc=%u, va=%u)\n",
                  millis(), (uint32_t)wordCount, (uint32_t)wordCount, (uint32_t)(wordStyles ? wordStyles->size() : 0),
                  (uint32_t)(bionicPrefixBytes ? bionicPrefixBytes->size() : 0),
                  (uint32_t)(wordSmallCaps ? wordSmallCaps->size() : 0),
                  (uint32_t)(wordVerticalAlign ? wordVerticalAlign->size() : 0));
    return;
  }

  auto slotIt = wordSlots.begin();
  auto styleIt = wordStyles ? wordStyles->begin() : std::vector<EpdFontFamily::Style>::const_iterator();
  auto prefixIt = bionicPrefixBytes ? bionicPrefixBytes->begin() : std::vector<uint8_t>::const_iterator();
  auto smallCapsIt = wordSmallCaps ? wordSmallCaps->begin() : std::vector<uint8_t>::const_iterator();
  auto underlineIt = wordUnderline ? wordUnderline->begin() : std::vector<uint8_t>::const_iterator();
  auto verticalAlignIt = wordVerticalAlign ? wordVerticalAlign->begin() : std::vector<uint8_t>::const_iterator();
  auto imgPathIt = wordImagePaths ? wordImagePaths->begin() : std::vector<std::string>::const_iterator();
  auto imgWIt = wordImageW ? wordImageW->begin() : std::vector<uint16_t>::const_iterator();
  auto imgHIt = wordImageH ? wordImageH->begin() : std::vector<uint16_t>::const_iterator();
  const bool hasBionicVector = bionicPrefixBytes && !bionicPrefixBytes->empty();
  const bool hasSmallCapsVector = wordSmallCaps && !wordSmallCaps->empty();
  const bool hasUnderlineVector = wordUnderline && !wordUnderline->empty();
  const bool hasVerticalAlignVector = wordVerticalAlign && !wordVerticalAlign->empty();
  const bool hasImages = wordImagePaths && !wordImagePaths->empty();

  // Underline sits just below the baseline.
  const int underlineY = y + renderer.text.getFontAscenderSize(fontId) + 1;
  const int lineHeight = renderer.text.getLineHeight(fontId);

  for (; slotIt != wordSlots.end(); ++slotIt) {
    const EpdFontFamily::Style wordStyle = wordStyles ? *styleIt : EpdFontFamily::REGULAR;
    const uint8_t prefixBytes = hasBionicVector ? *prefixIt : bionicPrefixDefault;
    const bool smallCaps = hasSmallCapsVector ? (*smallCapsIt != 0) : (smallCapsDefault != 0);
    const bool underline = hasUnderlineVector ? (*underlineIt != 0) : (underlineDefault != 0);
    const uint8_t verticalAlign = hasVerticalAlignVector ? *verticalAlignIt : verticalAlignDefault;
    const int startX = slotIt->xpos + x;
    int endX = startX;

    // Inline image word: draw the cached image vertically centered on the line and skip the text path.
    if (hasImages && imgPathIt != wordImagePaths->end() && !imgPathIt->empty()) {
      const int imgW = (wordImageW && imgWIt != wordImageW->end()) ? *imgWIt : 0;
      const int imgH = (wordImageH && imgHIt != wordImageH->end()) ? *imgHIt : 0;
      if (imgW > 0 && imgH > 0) {
        const int imgY = y + std::max(0, (lineHeight - imgH) / 2);
        ImageRender::create(renderer, *imgPathIt).render(startX, imgY, imgW, imgH, ImageRenderMode::OneBit);
      }
      if (hasBionicVector) ++prefixIt;
      if (hasSmallCapsVector) ++smallCapsIt;
      if (hasUnderlineVector) ++underlineIt;
      if (hasVerticalAlignVector) ++verticalAlignIt;
      ++imgPathIt;
      if (wordImageW && imgWIt != wordImageW->end()) ++imgWIt;
      if (wordImageH && imgHIt != wordImageH->end()) ++imgHIt;
      if (wordStyles) ++styleIt;
      continue;
    }

    if (prefixBytes == 0 || prefixBytes >= slotIt->text.size()) {
      endX = renderWordSegment(renderer, fontId, startX, y, slotIt->text, wordStyle, smallCaps, verticalAlign, black);
    } else {
      const std::string prefix = slotIt->text.substr(0, prefixBytes);
      const std::string suffix = slotIt->text.substr(prefixBytes);
      const auto prefixStyle = bionicStyleFor(wordStyle);
      const int suffixX =
          renderWordSegment(renderer, fontId, startX, y, prefix, prefixStyle, smallCaps, verticalAlign, black);
      endX = renderWordSegment(renderer, fontId, suffixX, y, suffix, wordStyle, smallCaps, verticalAlign, black);
    }
    if (underline && endX <= startX) {
      endX = startX + measureWordSegment(renderer, fontId, slotIt->text, wordStyle, smallCaps, verticalAlign);
    }
    if (underline && endX > startX) {
      renderer.line.render(startX, underlineY, endX - 1, underlineY, black);
    }
    if (hasBionicVector) {
      ++prefixIt;
    }
    if (hasSmallCapsVector) {
      ++smallCapsIt;
    }
    if (hasUnderlineVector) {
      ++underlineIt;
    }
    if (hasVerticalAlignVector) {
      ++verticalAlignIt;
    }
    if (hasImages) {
      if (imgPathIt != wordImagePaths->end()) ++imgPathIt;
      if (wordImageW && imgWIt != wordImageW->end()) ++imgWIt;
      if (wordImageH && imgHIt != wordImageH->end()) ++imgHIt;
    }
    if (wordStyles) {
      ++styleIt;
    }
  }
}

bool TextBlock::serialize(FsFile& file) const {
  const auto* wordStyles = extra && !extra->wordStyles.empty() ? &extra->wordStyles : nullptr;
  const auto* bionicPrefixBytes = extra ? &extra->bionicPrefixBytes : nullptr;
  const auto* wordSmallCaps = extra ? &extra->wordSmallCaps : nullptr;
  const auto* wordUnderline = extra ? &extra->wordUnderline : nullptr;
  const auto* wordVerticalAlign = extra ? &extra->wordVerticalAlign : nullptr;
  const auto* wordImagePaths = extra ? &extra->wordImagePaths : nullptr;
  const auto* wordImageW = extra ? &extra->wordImageW : nullptr;
  const auto* wordImageH = extra ? &extra->wordImageH : nullptr;
  const auto* wordFootnoteTargets = extra ? &extra->wordFootnoteTargets : nullptr;

  const size_t wordCount = wordSlots.size();
  if ((wordStyles && wordCount != wordStyles->size()) ||
      (bionicPrefixBytes && !bionicPrefixBytes->empty() && bionicPrefixBytes->size() != wordCount) ||
      (wordSmallCaps && !wordSmallCaps->empty() && wordSmallCaps->size() != wordCount) ||
      (wordUnderline && !wordUnderline->empty() && wordUnderline->size() != wordCount) ||
      (wordVerticalAlign && !wordVerticalAlign->empty() && wordVerticalAlign->size() != wordCount)) {
    Serial.printf(
        "[%lu] [TXB] Serialization failed: size mismatch (words=%u, xpos=%u, styles=%u, bionic=%u, sc=%u, va=%u)\n",
        millis(), wordCount, wordCount, wordStyles ? wordStyles->size() : 0,
        bionicPrefixBytes ? bionicPrefixBytes->size() : 0,
        wordSmallCaps ? wordSmallCaps->size() : 0,
        wordVerticalAlign ? wordVerticalAlign->size() : 0);
    return false;
  }

  serialization::writePod(file, static_cast<uint16_t>(wordCount));
  for (const auto& slot : wordSlots) serialization::writeString(file, slot.text);
  for (const auto& slot : wordSlots) serialization::writePod(file, slot.xpos);
  if (!wordStyles) {
    for (size_t i = 0; i < wordCount; ++i) serialization::writePod(file, EpdFontFamily::REGULAR);
  } else {
    for (auto s : *wordStyles) serialization::writePod(file, s);
  }
  if (!bionicPrefixBytes || bionicPrefixBytes->empty()) {
    for (size_t i = 0; i < wordCount; ++i) serialization::writePod(file, bionicPrefixDefault);
  } else {
    for (auto b : *bionicPrefixBytes) serialization::writePod(file, b);
  }
  if (!wordSmallCaps || wordSmallCaps->empty()) {
    for (size_t i = 0; i < wordCount; ++i) serialization::writePod(file, smallCapsDefault);
  } else {
    for (auto f : *wordSmallCaps) serialization::writePod(file, f);
  }
  if (!wordUnderline || wordUnderline->empty()) {
    for (size_t i = 0; i < wordCount; ++i) serialization::writePod(file, underlineDefault);
  } else {
    for (auto f : *wordUnderline) serialization::writePod(file, f);
  }
  if (!wordVerticalAlign || wordVerticalAlign->empty()) {
    for (size_t i = 0; i < wordCount; ++i) serialization::writePod(file, verticalAlignDefault);
  } else {
    for (auto f : *wordVerticalAlign) serialization::writePod(file, f);
  }
  // Inline image fields (path + display size), parallel to words. A single flag keeps text-only lines free of
  // any per-word image data on disk (and avoids allocating empty placeholders on load).
  const uint8_t hasImages = (wordImagePaths && !wordImagePaths->empty()) ? 1 : 0;
  serialization::writePod(file, hasImages);
  if (hasImages) {
    for (const auto& p : *wordImagePaths) serialization::writeString(file, p);
    if (wordImageW && wordImageW->size() == wordCount) {
      for (auto v : *wordImageW) serialization::writePod(file, v);
    } else {
      for (size_t i = 0; i < wordCount; ++i) serialization::writePod(file, static_cast<uint16_t>(0));
    }
    if (wordImageH && wordImageH->size() == wordCount) {
      for (auto v : *wordImageH) serialization::writePod(file, v);
    } else {
      for (size_t i = 0; i < wordCount; ++i) serialization::writePod(file, static_cast<uint16_t>(0));
    }
  }
  // Footnote/endnote link targets, parallel to words - same "one flag, skip entirely when absent" shape as
  // the image fields above, so plain text (the overwhelming majority of lines) pays nothing extra on disk.
  const uint8_t hasFootnoteLinks = (wordFootnoteTargets && !wordFootnoteTargets->empty()) ? 1 : 0;
  serialization::writePod(file, hasFootnoteLinks);
  if (hasFootnoteLinks) {
    for (const auto& t : *wordFootnoteTargets) serialization::writeString(file, t);
  }
  serialization::writePod(file, style);

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(FsFile& file) {
  uint16_t wc;
  std::list<std::string> words;
  std::list<int16_t> wordXpos;
  std::list<EpdFontFamily::Style> wordStyles;
  std::list<uint8_t> bionicPrefixBytes;
  std::list<uint8_t> wordSmallCaps;
  std::list<uint8_t> wordUnderline;
  std::list<uint8_t> wordVerticalAlign;
  std::list<std::string> wordImagePaths;
  std::list<uint16_t> wordImageW;
  std::list<uint16_t> wordImageH;
  std::list<std::string> wordFootnoteTargets;
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
  if (std::all_of(wordStyles.begin(), wordStyles.end(),
                  [](EpdFontFamily::Style style) { return style == EpdFontFamily::REGULAR; })) {
    wordStyles.clear();
  }
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
  uint8_t hasFootnoteLinks = 0;
  serialization::readPod(file, hasFootnoteLinks);
  if (hasFootnoteLinks) {
    wordFootnoteTargets.resize(wc);
    for (auto& t : wordFootnoteTargets) serialization::readString(file, t);
  }
  serialization::readPod(file, style);

  return std::unique_ptr<TextBlock>(
      new TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles), std::move(bionicPrefixBytes),
                    std::move(wordSmallCaps), style, std::move(wordUnderline), std::move(wordVerticalAlign),
                    std::move(wordImagePaths), std::move(wordImageW), std::move(wordImageH),
                    std::move(wordFootnoteTargets)));
}
