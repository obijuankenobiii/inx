#include "SearchText.h"

#include <GfxRenderer.h>

#include "system/Fonts.h"

int SearchText::top() { return 80; }

void SearchText::render(const GfxRenderer& renderer, const std::string& value, const char* placeholder) {
  constexpr int margin = 20;
  constexpr int button = 40;
  constexpr int gap = 10;
  const int x = margin;
  const int y = top();
  const int width = renderer.getScreenWidth() - margin * 2 - button - gap;

  renderer.rectangle.fill(x, y, width, height, false, true);
  renderer.rectangle.render(x, y, width, height, true, true);

  const std::string shown = value.empty() ? (placeholder ? placeholder : "Search books") : value;
  const bool hint = value.empty();
  const int font = systemFontId();
  const int textY = y + (height - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, x + 14, textY, shown.c_str(), !hint);
}
