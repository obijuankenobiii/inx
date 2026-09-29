#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "util/LibraryIndex.h"

class GfxRenderer;

namespace views::library {

/** Inx-pro-style 2x2 thumbnail view. */
class Thumbnail final {
 public:
  Thumbnail(GfxRenderer& renderer, const std::vector<LibraryIndex::Book>& items);

  static int itemsPerPage();

  void render(int selectedIndex = -1, int page = 0) const;
  void renderSelection(int selectedIndex, int page = 0) const;

 private:
  struct Rect {
    int x;
    int y;
    int width;
    int height;
  };

  GfxRenderer& renderer_;
  const std::vector<LibraryIndex::Book>& items_;
  mutable std::unordered_map<std::string, std::vector<std::string>> folderCoverCache_;
  mutable std::unordered_map<std::string, int> folderBookCountCache_;

  int top() const;
  void itemBounds(int index, Rect& cell, Rect& cover) const;
  void drawItem(const LibraryIndex::Book& item, const Rect& cell, const Rect& cover, bool selected) const;
  void drawBook(const LibraryIndex::Book& item, const Rect& cover) const;
  void drawFolder(const LibraryIndex::Book& item, const Rect& cover) const;
  void drawSelection(const LibraryIndex::Book& item, const Rect& cell, const Rect& cover) const;
  void drawSelectedTitle(const LibraryIndex::Book& item, const Rect& cell, const Rect& cover) const;

  static std::string displayTitle(const LibraryIndex::Book& item);
  static std::string thumbnailPath(const std::string& bookPath);
  const std::vector<std::string>& folderCovers(const LibraryIndex::Book& folder) const;
  int folderBookCount(const LibraryIndex::Book& folder) const;
  bool renderImage(const std::string& path, int x, int y, int width, int height, bool cropToFill,
                   bool cropFromTop = false) const;
};

}  // namespace views::library
