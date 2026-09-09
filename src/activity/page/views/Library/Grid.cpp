#include "Grid.h"

#include <BitmapRender.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <cctype>
#include <string>

#include "images/BookLarge.h"
#include "images/FolderLarge.h"
#include "images/ImageLarge.h"
#include "system/Fonts.h"
#include "system/UiLayout.h"

namespace views {
namespace library {
namespace {

bool isImage(const std::string& path) {
  std::string value = path;
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  for (const char* suffix : {".bmp", ".jpg", ".jpeg", ".png"}) {
    const size_t length = std::char_traits<char>::length(suffix);
    if (value.size() >= length && value.compare(value.size() - length, length, suffix) == 0) {
      return true;
    }
  }
  return false;
}

void titleLines(const GfxRenderer& renderer, const std::string& value, const int font, const int width,
                std::string& first, std::string& second) {
  first.clear();
  second.clear();
  if (value.empty()) {
    return;
  }
  if (renderer.text.getWidth(font, value.c_str()) <= width) {
    first = value;
    return;
  }

  size_t split = value.find(' ');
  size_t best = std::string::npos;
  while (split != std::string::npos) {
    if (renderer.text.getWidth(font, value.substr(0, split).c_str()) > width) {
      break;
    }
    best = split;
    split = value.find(' ', split + 1);
  }
  if (best == std::string::npos) {
    first = renderer.text.truncate(font, value.c_str(), width);
    return;
  }
  first = value.substr(0, best);
  while (best < value.size() && value[best] == ' ') {
    ++best;
  }
  second = renderer.text.truncate(font, value.substr(best).c_str(), width);
}

}  // namespace

Grid::Grid(GfxRenderer& renderer, const std::vector<LibraryIndex::Book>& items) : renderer_(renderer), items_(items) {}

int Grid::top() const { return UiLayout::MENU_HEIGHT; }

int Grid::visibleHeight() const {
  return std::max(1, renderer_.getScreenHeight() - top() - UiLayout::MENU_BOTTOM_HEIGHT - 10);
}

void Grid::itemBounds(const int index, int& x, int& y, int& width, int& height) const {
  const int availableWidth = renderer_.getScreenWidth() - margin * 2;
  const int availableHeight = visibleHeight();
  const int cellWidth = (availableWidth - gapX * (columns - 1)) / columns;
  const int cellHeight = (availableHeight - minGapY * (rows - 1)) / rows;
  width = std::min(maxFrame, cellWidth);
  height = std::max(96, std::min(maxFrame, cellHeight));
  const int remainingWidth = availableWidth - columns * width;
  const int actualGapX = std::max(gapX, remainingWidth / (columns - 1));
  const int remainingHeight = availableHeight - rows * height;
  const int actualGapY = std::max(minGapY, remainingHeight / (rows - 1));
  const int blockWidth = columns * width + (columns - 1) * actualGapX;
  const int blockHeight = rows * height + (rows - 1) * actualGapY;
  const int left = margin + std::max(0, (availableWidth - blockWidth) / 2);
  const int startY = top() + std::max(0, (availableHeight - blockHeight) / 2);
  x = left + (index % columns) * (width + actualGapX);
  y = startY + (index / columns) * (height + actualGapY);
}

std::string Grid::displayTitle(const LibraryIndex::Book& item) {
  if (!item.author.empty()) {
    return item.author + " - " + item.title;
  }
  return item.title;
}

void Grid::drawItem(const LibraryIndex::Book& item, const int x, const int y, const int width, const int height,
                    const bool selected) const {
  constexpr int iconPadding = 8;
  const int iconX = x + iconPadding;
  const int iconY = y + iconPadding;
  const int iconWidth = std::max(8, width - iconPadding * 2);
  const int iconHeight = std::max(8, height - labelHeight - labelGap - iconPadding * 2);
  const int iconSize = std::min(72, std::max(32, std::min(iconWidth, iconHeight) - 12));
  const int drawX = iconX + (iconWidth - iconSize) / 2;
  const int drawY = iconY + (iconHeight - iconSize) / 2;
  const uint8_t* icon = item.type == LibraryIndex::Book::Type::FOLDER ? FolderLarge
                                                                        : (isImage(item.path) ? ImageLarge : BookLarge);
  if (selected) {
    renderer_.rectangle.fill(x, y, width, height, true, true, true);
  }
  renderer_.bitmap.icon(icon, drawX, drawY, iconSize, iconSize, BitmapRender::Orientation::None, selected);

  constexpr int font = MONTSERRAT_10_FONT_ID;
  const int available = std::max(20, width - 10);
  std::string first;
  std::string second;
  titleLines(renderer_, displayTitle(item), font, available, first, second);
  const int lineHeight = renderer_.text.getLineHeight(font);
  const int lineCount = second.empty() ? 1 : 2;
  const int labelY = iconY + iconHeight + labelGap + (labelHeight - lineCount * lineHeight) / 2;
  const int firstWidth = renderer_.text.getWidth(font, first.c_str());
  renderer_.text.render(font, x + (width - firstWidth) / 2, labelY, first.c_str(), !selected);
  if (!second.empty()) {
    const int secondWidth = renderer_.text.getWidth(font, second.c_str());
    renderer_.text.render(font, x + (width - secondWidth) / 2, labelY + lineHeight, second.c_str(), !selected);
  }
}

void Grid::render(const int selectedIndex, const int page) const {
  const int start = std::max(0, page) * itemsPerPage();
  if (start >= static_cast<int>(items_.size())) {
    return;
  }
  const int count = std::min(itemsPerPage(), static_cast<int>(items_.size()) - start);
  for (int index = 0; index < count; ++index) {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    itemBounds(index, x, y, width, height);
    const int itemIndex = start + index;
    drawItem(items_[static_cast<size_t>(itemIndex)], x, y, width, height, itemIndex == selectedIndex);
  }
}

}  // namespace library
}  // namespace views
