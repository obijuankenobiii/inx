#pragma once

#include <EpdFontFamily.h>
#include <string>

class GfxRenderer;

class TextRender {
 public:
  explicit TextRender(GfxRenderer& g) : gfx(g) {}

  int getWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  int getHeight(int fontId) const;
  int getFontAscenderSize(int fontId) const;
  int getLineHeight(int fontId) const;
  int getSpaceWidth(int fontId) const;
  void prewarm(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  std::string truncate(int fontId, const char* text, int maxWidth,
                       EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void rotated90CW(int fontId, int x, int y, const char* text, bool black = true,
                       EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void render(int fontId, int x, int y, const char* text, bool black = true,
            EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void centered(int fontId, int y, const char* text, bool black = true,
                    EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;

 private:
  void renderChar(const EpdFontFamily& fontFamily, uint32_t cp, int* x, const int* y, bool pixelState,
                  EpdFontFamily::Style style) const;
  int getStreamingTextWidth(const EpdFontFamily& family, const char* text, EpdFontFamily::Style style) const;
  GfxRenderer& gfx;
};
