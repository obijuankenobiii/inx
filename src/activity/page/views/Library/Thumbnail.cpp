#include <BitmapRender.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include "Thumbnail.h"

#include <algorithm>
#include <cctype>
#include <functional>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/UiLayout.h"
#include "activity/page/components/widget/WidgetRender.h"

namespace views::library {
namespace {

constexpr int kCoverPadding = 4;
constexpr int kTitleGap = 5;
constexpr int kSelectionPadding = 5;
constexpr int kFolderBooks = 3;

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool endsWith(const std::string& value, const char* suffix) {
  const size_t length = std::char_traits<char>::length(suffix);
  return value.size() >= length && value.compare(value.size() - length, length, suffix) == 0;
}

void drawContrastFrame(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                       const bool rounded) {
  if (width < 1 || height < 1) return;
  renderer.rectangle.render(x, y, width, height, true, rounded);
  if (width > 3 && height > 3) {
    renderer.rectangle.render(x + 1, y + 1, width - 2, height - 2, false, rounded);
  }
}

}  // namespace

Thumbnail::Thumbnail(GfxRenderer& renderer, const std::vector<LibraryIndex::Book>& items)
    : renderer_(renderer), items_(items) {}

int Thumbnail::itemsPerPage() { return 4; }

int Thumbnail::top() const { return UiLayout::MENU_HEIGHT - 20; }

void Thumbnail::itemBounds(const int index, Rect& cell, Rect& cover) const {
  constexpr int regularColumns = 2;
  constexpr int regularRows = 2;
  constexpr int regularMargin = 20;
  constexpr int regularGap = 20;
  constexpr int regularRowGap = 10;
  constexpr int regularTop = UiLayout::MENU_HEIGHT + 10;
  const int availableWidth = std::max(1, renderer_.getScreenWidth() - regularMargin * 2);
  const int availableHeight = std::max(1, renderer_.getScreenHeight() - regularTop - UiLayout::MENU_BOTTOM_HEIGHT);
  const int cellWidth = std::max(40, (availableWidth - regularGap * (regularColumns - 1)) / regularColumns);
  const int cellHeight = std::max(40, (availableHeight - regularRowGap * (regularRows - 1)) / regularRows);
  const int gridWidth = cellWidth * regularColumns + regularGap * (regularColumns - 1);
  const int gridHeight = cellHeight * regularRows + regularRowGap * (regularRows - 1);
  const int startX = regularMargin + std::max(0, (availableWidth - gridWidth) / 2);
  const int startY = regularTop + std::max(0, (availableHeight - gridHeight) / 2);
  cell = {startX + (index % regularColumns) * (cellWidth + regularGap),
          startY + (index / regularColumns) * (cellHeight + regularRowGap), cellWidth, cellHeight};
  const int lineHeight = renderer_.text.getLineHeight(systemFontId());
  const int titleHeight = lineHeight + 10;
  cover = {cell.x + 3, cell.y + 3, std::max(8, cell.width - 6),
           std::max(8, cell.height - titleHeight - 6)};
}

std::string Thumbnail::displayTitle(const LibraryIndex::Book& item) {
  if (!item.title.empty()) return item.author.empty() ? item.title : item.author + " - " + item.title;
  const size_t slash = item.path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = item.path.find_last_of('.');
  const size_t end = dot == std::string::npos || dot < start ? item.path.size() : dot;
  const std::string title = item.path.substr(start, end - start);
  return item.author.empty() ? title : item.author + " - " + title;
}

std::string Thumbnail::thumbnailPath(const std::string& bookPath) {
  const std::string value = lower(bookPath);
  const char* root = (endsWith(value, ".xtc") || endsWith(value, ".xtch")) ? "/.metadata/xtc" : "/.metadata/epub";
  const std::string directory = std::string(root) + "/" + std::to_string(std::hash<std::string>{}(bookPath));
  for (const char* name : {"thumb.jpg", "thumb.png", "thumb.bmp"}) {
    const std::string path = directory + "/" + name;
    if (SdMan.exists(path.c_str())) return path;
  }
  return {};
}

const std::vector<std::string>& Thumbnail::folderCovers(const LibraryIndex::Book& folder) const {
  const auto cached = folderCoverCache_.find(folder.path);
  if (cached != folderCoverCache_.end()) return cached->second;

  std::vector<std::string> covers;
  const std::string folderPath = folder.path;
  const std::string prefix = folderPath == "/" ? "/" : folderPath + "/";
  int bookCount = 0;
  LibraryIndex::visit([&](LibraryIndex::Book& child) {
    if (child.type != LibraryIndex::Book::Type::BOOK || child.path.compare(0, prefix.size(), prefix) != 0) return true;
    ++bookCount;
    if (static_cast<int>(covers.size()) < kFolderBooks) {
      const std::string path = thumbnailPath(child.path);
      if (!path.empty()) covers.push_back(path);
    }
    return true;
  });
  folderBookCountCache_[folder.path] = bookCount;
  const auto inserted = folderCoverCache_.emplace(folder.path, std::move(covers));
  return inserted.first->second;
}

int Thumbnail::folderBookCount(const LibraryIndex::Book& folder) const {
  const auto cached = folderBookCountCache_.find(folder.path);
  if (cached != folderBookCountCache_.end()) return cached->second;
  folderCovers(folder);
  const auto count = folderBookCountCache_.find(folder.path);
  return count == folderBookCountCache_.end() ? 0 : count->second;
}

bool Thumbnail::renderImage(const std::string& path, const int x, const int y, const int width,
                            const int height, const bool cropToFill, const bool cropFromTop) const {
  if (path.empty() || width <= 0 || height <= 0) return false;
  ImageRender::Options options;
  options.cropToFill = cropToFill;
  options.cropFromTop = cropFromTop;
  options.useDisplayCache = true;
  options.roundedOutside = SETTINGS.bitmapRoundedCorners != 0 ? BitmapRender::RoundedOutside::PaperOutside
                                                               : BitmapRender::RoundedOutside::None;
  return ImageRender::create(renderer_, path).render(x, y, width, height, options);
}

void Thumbnail::drawBook(const LibraryIndex::Book& item, const Rect& cover) const {
  const std::string path = thumbnailPath(item.path);
  int imageX = cover.x;
  int imageY = cover.y;
  int imageWidth = cover.width;
  int imageHeight = cover.height;
  if (!path.empty()) {
    int sourceWidth = 0;
    int sourceHeight = 0;
    if (ImageRender::getDimensions(path, &sourceWidth, &sourceHeight) && sourceWidth > 0 && sourceHeight > 0) {
      const float scale = std::min(static_cast<float>(cover.width) / sourceWidth,
                                   static_cast<float>(cover.height) / sourceHeight);
      imageWidth = std::max(1, static_cast<int>(sourceWidth * scale));
      imageHeight = std::max(1, static_cast<int>(sourceHeight * scale));
      imageX += (cover.width - imageWidth) / 2;
      imageY += (cover.height - imageHeight) / 2;
    }
  }
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  renderer_.rectangle.fill(imageX, imageY, imageWidth, imageHeight, false, rounded);
  if (!renderImage(path, imageX, imageY, imageWidth, imageHeight, false, false)) {
    renderer_.rectangle.render(imageX, imageY, imageWidth, imageHeight, true, rounded);
  } else {
    drawContrastFrame(renderer_, imageX, imageY, imageWidth, imageHeight, rounded);
  }
}

void Thumbnail::drawFolder(const LibraryIndex::Book& item, const Rect& cover) const {
  const std::vector<std::string>& covers = folderCovers(item);
  if (!covers.empty()) {
    constexpr int layerStep = 9;
    const int backingHeight = std::max(8, cover.height - 18);
    const int thirdHeight = std::max(8, cover.height - 12);
    const int secondHeight = std::max(8, cover.height - 6);
    const int frontWidth = std::max(12, cover.width - layerStep * 2);
    const int frontX = cover.x + layerStep * 2;
    renderer_.rectangle.fill(cover.x, cover.y + (cover.height - backingHeight) / 2, frontWidth,
                             backingHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer_.rectangle.render(cover.x, cover.y + (cover.height - backingHeight) / 2, frontWidth,
                               backingHeight, true);
    const auto drawLayer = [&](const std::string& path, const int x, const int y, const int width,
                               const int height) {
      renderer_.rectangle.fill(x, y, width, height, false);
      renderImage(path, x, y, width, height, true);
      renderer_.rectangle.render(x, y, width, height, true);
    };
    if (covers.size() > 2) {
      drawLayer(covers[2], cover.x, cover.y + (cover.height - thirdHeight) / 2, layerStep * 2, thirdHeight);
    }
    if (covers.size() > 1) {
      drawLayer(covers[1], cover.x + layerStep, cover.y + (cover.height - secondHeight) / 2, layerStep,
                secondHeight);
    }
    drawLayer(covers[0], frontX, cover.y, frontWidth, cover.height);

    const int count = item.bookCount > 0 ? item.bookCount : folderBookCount(item);
    if (count > 0) {
      constexpr int paddingX = 6;
      constexpr int paddingY = 4;
      constexpr int margin = 5;
      const int font = systemFontId();
      const std::string label = "+" + std::to_string(count);
      const int badgeWidth = renderer_.text.getWidth(font, label.c_str()) + paddingX * 2;
      const int badgeHeight = renderer_.text.getLineHeight(font) + paddingY * 2;
      const int badgeX = frontX + std::max(0, frontWidth - badgeWidth - margin);
      const int badgeY = cover.y + std::max(0, cover.height - badgeHeight - margin);
      renderer_.rectangle.fill(badgeX, badgeY, badgeWidth, badgeHeight,
                               static_cast<int>(GfxRenderer::FillTone::Ink), true);
      renderer_.rectangle.render(badgeX, badgeY, badgeWidth, badgeHeight, false, true);
      renderer_.text.render(font, badgeX + paddingX, badgeY + paddingY, label.c_str(), false);
    }
    return;
  }

  renderer_.rectangle.fill(cover.x, cover.y, cover.width, cover.height, false);
  renderer_.rectangle.render(cover.x, cover.y, cover.width, cover.height, true);
  const int count = item.bookCount > 0 ? item.bookCount : folderBookCount(item);
  if (count > 0) {
    constexpr int paddingX = 6;
    constexpr int paddingY = 4;
    constexpr int margin = 5;
    const int font = systemFontId();
    const std::string label = "+" + std::to_string(count);
    const int badgeWidth = renderer_.text.getWidth(font, label.c_str()) + paddingX * 2;
    const int badgeHeight = renderer_.text.getLineHeight(font) + paddingY * 2;
    const int badgeX = cover.x + std::max(0, cover.width - badgeWidth - margin);
    const int badgeY = cover.y + std::max(0, cover.height - badgeHeight - margin);
    renderer_.rectangle.fill(badgeX, badgeY, badgeWidth, badgeHeight,
                             static_cast<int>(GfxRenderer::FillTone::Ink), true);
    renderer_.rectangle.render(badgeX, badgeY, badgeWidth, badgeHeight, false, true);
    renderer_.text.render(font, badgeX + paddingX, badgeY + paddingY, label.c_str(), false);
  }
}

void Thumbnail::drawSelectedTitle(const LibraryIndex::Book& item, const Rect& cell, const Rect& cover) const {
  const int font = systemFontId();
  const std::string title = renderer_.text.truncate(font, displayTitle(item).c_str(), std::max(8, cell.width - 4));
  const int textWidth = renderer_.text.getWidth(font, title.c_str());
  renderer_.text.render(font, cover.x + (cover.width - textWidth) / 2, cover.y + cover.height + kTitleGap,
                        title.c_str(), true, EpdFontFamily::REGULAR);
}

void Thumbnail::drawSelection(const LibraryIndex::Book& item, const Rect& cell, const Rect& cover) const {
  const int font = systemFontId();
  const int lineHeight = renderer_.text.getLineHeight(font);
  const int selectionX = cover.x - kSelectionPadding;
  const int selectionY = cover.y - kSelectionPadding;
  const int selectionWidth = cover.width + kSelectionPadding * 2;
  const int titleHeight = kTitleGap + lineHeight;
  const int selectionHeight = cover.height + titleHeight + kSelectionPadding * 2;
  widget::support::drawDitherRect(renderer_, selectionX, selectionY, selectionWidth, selectionHeight);
  renderer_.rectangle.render(selectionX, selectionY, selectionWidth, selectionHeight, true, false, false);
}

void Thumbnail::drawItem(const LibraryIndex::Book& item, const Rect& cell, const Rect& cover,
                         const bool selected) const {
  if (selected) drawSelection(item, cell, cover);
  if (item.type == LibraryIndex::Book::Type::FOLDER) {
    drawFolder(item, cover);
  } else {
    drawBook(item, cover);
  }
  drawSelectedTitle(item, cell, cover);
}

void Thumbnail::render(const int selectedIndex, const int page) const {
  const int start = std::max(0, page) * itemsPerPage();
  if (start >= static_cast<int>(items_.size())) return;
  const int count = std::min(itemsPerPage(), static_cast<int>(items_.size()) - start);
  for (int index = 0; index < count; ++index) {
    Rect cell{}, cover{};
    itemBounds(index, cell, cover);
    const int itemIndex = start + index;
    drawItem(items_[static_cast<size_t>(itemIndex)], cell, cover, itemIndex == selectedIndex);
  }
}

void Thumbnail::renderSelection(const int selectedIndex, const int page) const {
  const int start = std::max(0, page) * itemsPerPage();
  const int localIndex = selectedIndex - start;
  if (localIndex < 0 || localIndex >= itemsPerPage() || selectedIndex >= static_cast<int>(items_.size())) return;
  Rect cell{}, cover{};
  itemBounds(localIndex, cell, cover);
  drawSelection(items_[static_cast<size_t>(selectedIndex)], cell, cover);
  drawSelectedTitle(items_[static_cast<size_t>(selectedIndex)], cell, cover);
}

}  // namespace views::library
