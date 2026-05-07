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
  std::string truncate(int fontId, const char* text, int maxWidth,
                       EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void rotated90CW(int fontId, int x, int y, const char* text, bool black = true,
                       EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void render(int fontId, int x, int y, const char* text, bool black = true,
            EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  void centered(int fontId, int y, const char* text, bool black = true,
                    EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;

 private:
  GfxRenderer& gfx;
};
