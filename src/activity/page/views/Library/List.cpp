#include "List.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <utility>

#include "images/BookSmall.h"
#include "images/Folder.h"
#include "images/Star.h"
#include "system/Fonts.h"
#include "system/UiLayout.h"

namespace views {
namespace library {

List::List(GfxRenderer& renderer, const std::vector<LibraryIndex::Book>& items,
           std::function<bool(const LibraryIndex::Book&)> isFavorite)
    : renderer_(renderer), items_(items), isFavorite_(std::move(isFavorite)) {}

std::string List::displayTitle(const LibraryIndex::Book& item) {
  std::string title = item.title;
  if (title.empty()) {
    const size_t slash = item.path.find_last_of('/');
    const size_t start = slash == std::string::npos ? 0 : slash + 1;
    const size_t dot = item.path.find_last_of('.');
    const size_t end = dot == std::string::npos || dot < start ? item.path.size() : dot;
    title = item.path.substr(start, end - start);
  }
  return item.author.empty() ? title : item.author + " - " + title;
}

void List::render(const int selectedIndex) const {
  const int start = std::max(0, selectedIndex < 0 ? 0 : selectedIndex / itemsPerPage()) * itemsPerPage();
  if (start >= static_cast<int>(items_.size())) return;

  const int count = std::min(itemsPerPage(), static_cast<int>(items_.size()) - start);
  const int width = renderer_.getScreenWidth();
  const int font = MONTSERRAT_12_FONT_ID;
  const int top = UiLayout::MENU_HEIGHT;

  for (int row = 0; row < count; ++row) {
    const int itemIndex = start + row;
    const LibraryIndex::Book& item = items_[static_cast<size_t>(itemIndex)];
    const int y = top + row * rowHeight;
    const bool selected = itemIndex == selectedIndex;
    if (selected) {
      renderer_.rectangle.fill(0, y, width, rowHeight, true);
    }

    const uint8_t* icon = item.type == LibraryIndex::Book::Type::FOLDER ? Folder : BookSmall;
    renderer_.bitmap.icon(icon, 20, y + (rowHeight - 24) / 2, 24, 24, BitmapRender::Orientation::None, selected);

    const bool favorite = isFavorite_ && isFavorite_(item);
    const int favoriteSize = 24;
    const int available = std::max(40, width - 54 - 20 - (favorite ? favoriteSize + 10 : 0));
    const std::string label = renderer_.text.truncate(font, displayTitle(item).c_str(), available);
    renderer_.text.render(font, 54, y + (rowHeight - renderer_.text.getLineHeight(font)) / 2, label.c_str(), !selected);
    if (favorite) {
      renderer_.bitmap.icon(Star, width - 20 - favoriteSize, y + (rowHeight - favoriteSize) / 2, favoriteSize,
                            favoriteSize, BitmapRender::Orientation::None, selected);
    }
    if (row + 1 < count) {
      renderer_.line.render(0, y + rowHeight - 1, width, y + rowHeight - 1, true, LineRender::Style::Dotted);
    }
  }
}

}  // namespace library
}  // namespace views
