#include "Button.h"

#include <GfxRenderer.h>

#include "system/Fonts.h"

int Button::width(const GfxRenderer& renderer, const char* label, const int font) {
  return renderer.text.getWidth(font, label ? label : "") + horizontalPadding * 2;
}

void Button::render(const GfxRenderer& renderer, const ButtonBounds& bounds, const char* label, const bool fill,
                   const int font) {
  renderer.rectangle.fill(bounds.x, bounds.y, bounds.width, bounds.height, fill);
  renderer.rectangle.render(bounds.x, bounds.y, bounds.width, bounds.height, true);

  const char* text = label ? label : "";
  const int textWidth = renderer.text.getWidth(font, text);
  const int textY = bounds.y + (bounds.height - renderer.text.getLineHeight(font)) / 2;
  const int textX = bounds.x + (bounds.width - textWidth) / 2;
  renderer.text.render(font, textX, textY, text, !fill, EpdFontFamily::REGULAR);
}

