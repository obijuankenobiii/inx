#pragma once

#include <functional>
#include <string>
#include <vector>

#include "util/LibraryIndex.h"

class GfxRenderer;

namespace views {
namespace library {

/** Pro-style compact library list. Selection and navigation remain owned by Library. */
class List final {
 public:
  List(GfxRenderer& renderer, const std::vector<LibraryIndex::Book>& items,
       std::function<bool(const LibraryIndex::Book&)> isFavorite);

  static constexpr int itemsPerPage() { return 10; }
  void render(int selectedIndex = -1) const;

 private:
  static constexpr int rowHeight = 66;

  GfxRenderer& renderer_;
  const std::vector<LibraryIndex::Book>& items_;
  std::function<bool(const LibraryIndex::Book&)> isFavorite_;

  static std::string displayTitle(const LibraryIndex::Book& item);
};

}  // namespace library
}  // namespace views
