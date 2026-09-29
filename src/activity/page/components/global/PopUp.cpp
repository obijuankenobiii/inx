#include "PopUp.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "system/Fonts.h"
#include "system/UiLayout.h"

PopUpBounds PopUp::bounds(const GfxRenderer& renderer, const int count, const int contentTop) {
  constexpr int margin = 20;
  constexpr int width = 300;
  constexpr int row = UiLayout::LIST_ITEM_HEIGHT;
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int boxWidth = std::min(screenWidth - margin * 2, width);
  const int header = UiLayout::LIST_ITEM_HEIGHT - 4;
  const int boxX = (screenWidth - boxWidth) / 2;
  const int requestedRows = std::min(maxRows, std::max(0, count));
  // The popup is rendered last. Keep its paper fill inside the page content
  // region so it cannot cover the shared top or bottom shell.
  const int safeTop = contentTop > 0 ? contentTop + 8 : UiLayout::MENU_HEIGHT;
  const int safeBottom = screenHeight - UiLayout::MENU_BOTTOM_HEIGHT;
  const int contentHeight = std::max(header + row, safeBottom - safeTop);
  const int rows = std::min(requestedRows, std::max(1, (contentHeight - header) / row));
  const int boxHeight = header + rows * row;
  const int boxY = safeTop + std::max(0, (contentHeight - boxHeight) / 2);
  return {boxX, boxY, boxWidth, boxHeight, header, row, rows};
}

void PopUp::background(GfxRenderer& renderer, const PopUpBounds& box) {
  renderer.rectangle.fill(box.x, box.y, box.width, box.height, false);
}

void PopUp::title(GfxRenderer& renderer, const PopUpBounds& box, const std::string& value) {
  const int font = systemFontId();
  const std::string shown = renderer.text.truncate(font, value.c_str(), box.width - 32, EpdFontFamily::BOLD);
  const int y = box.y + (box.header - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, box.x + 16, y, shown.c_str(), true, EpdFontFamily::BOLD);
}

void PopUp::list(GfxRenderer& renderer, const PopUpBounds& box, const std::vector<std::string>& values,
                 const int selected, const int scroll) {
  const int font = systemFontId();
  const int maxScroll = std::max(0, static_cast<int>(values.size()) - box.rows);
  const int start = std::max(0, std::min(scroll, maxScroll));
  for (int i = 0; i < box.rows; ++i) {
    const int index = start + i;
    if (index >= static_cast<int>(values.size())) break;
    const int y = box.y + box.header + i * box.row;
    const bool active = index == selected;
    if (active) renderer.rectangle.fill(box.x + 1, y, box.width - 2, box.row, true);
    const std::string shown = renderer.text.truncate(font, values[static_cast<size_t>(index)].c_str(), box.width - 44);
    const int textY = y + (box.row - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, box.x + 20, textY, shown.c_str(), !active,
                         active ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (i + 1 < box.rows && index + 1 < static_cast<int>(values.size())) {
      renderer.line.render(box.x, y + box.row, box.x + box.width, y + box.row, !active,
                           LineRender::Style::Dotted);
    }
  }
  if (static_cast<int>(values.size()) > box.rows) {
    const int trackX = box.x + box.width - 8;
    const int trackY = box.y + box.header;
    const int trackH = box.rows * box.row;
    const int thumbH = std::max(8, trackH * box.rows / static_cast<int>(values.size()));
    const int thumbY = trackY + start * std::max(1, trackH - thumbH) / maxScroll;
    renderer.line.render(trackX, trackY, trackX, trackY + trackH - 1, true, LineRender::Style::Dotted);
    renderer.rectangle.fill(trackX - 1, thumbY, 3, thumbH, true);
  }
}

void PopUp::border(GfxRenderer& renderer, const PopUpBounds& box) {
  renderer.line.render(box.x, box.y + box.header, box.x + box.width, box.y + box.header, true);
  renderer.rectangle.render(box.x, box.y, box.width, box.height, true);
}
