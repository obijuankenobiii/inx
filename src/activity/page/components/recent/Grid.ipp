void recent::Grid::render(RecentActivity& self, int startY) { self.renderGrid(startY); }

void recent::Grid3x3::render(RecentActivity& self, int startY) { self.renderIcons(startY); }

/**
 * Renders the complete grid view including all visible books.
 */
void RecentActivity::renderGrid(int startY) {
  int totalBooks = static_cast<int>(recentBooks.size());
  if (totalBooks == 0) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, startY + 150, "No recent books");
    return;
  }

  int visibleRows = getVisibleRows();
  int startRow = scrollOffset;
  int endRow = std::min(startRow + visibleRows, (totalBooks + GRID_COLS - 1) / GRID_COLS);

  for (int row = startRow; row < endRow; ++row) {
    for (int col = 0; col < GRID_COLS; ++col) {
      int bookIdx = row * GRID_COLS + col;
      if (bookIdx >= totalBooks) break;

      bool isSelected = !suppressBufferedSelection_ && (selectorIndex == bookIdx);
      renderGridItem(col, row - startRow, startY, recentBooks[bookIdx], isSelected);
    }
  }
}

void RecentActivity::renderIcons(int startY) {
  const int totalBooks = static_cast<int>(recentBooks.size());
  if (totalBooks == 0) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, startY + 150, "No recent books");
    return;
  }

  constexpr int kCols = ICON_COLS;
  constexpr int kRowsVisible = ICON_ROWS;
  constexpr int kGap = 8;
  constexpr int kMarginX = 10;
  constexpr int kMarginY = 8;
  constexpr int kInnerPad = 4;
  const int screenW = renderer.getScreenWidth();
  const int contentBottom =
      INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) : renderer.getScreenHeight() - 54;
  const int availW = std::max(1, screenW - kMarginX * 2);
  const int availH = std::max(1, contentBottom - startY - kMarginY * 2);
  const int frameW = std::max(40, (availW - (kCols - 1) * kGap) / kCols);
  const int frameH = std::max(40, (availH - (kRowsVisible - 1) * kGap) / kRowsVisible);
  const int blockW = kCols * frameW + (kCols - 1) * kGap;
  const int blockH = kRowsVisible * frameH + (kRowsVisible - 1) * kGap;
  const int row0X = kMarginX + std::max(0, (availW - blockW) / 2);
  const int blockTop = startY + kMarginY + std::max(0, (availH - blockH) / 2);
  const bool rr = SETTINGS.bitmapRoundedCorners != 0;

  const int startRow = scrollOffset;
  const int endRow = std::min(startRow + kRowsVisible, (totalBooks + kCols - 1) / kCols);

  for (int row = startRow; row < endRow; ++row) {
    for (int col = 0; col < kCols; ++col) {
      const int bookIdx = row * kCols + col;
      if (bookIdx >= totalBooks) {
        break;
      }
      const int visualRow = row - startRow;
      const int boxX = row0X + col * (frameW + kGap);
      const int boxY = blockTop + visualRow * (frameH + kGap);
      const bool selected = !suppressBufferedSelection_ && (selectorIndex == bookIdx);

      const int innerX = boxX + kInnerPad;
      const int innerY = boxY + kInnerPad;
      const int innerW = std::max(8, frameW - kInnerPad * 2);
      const int innerH = std::max(8, frameH - kInnerPad * 2);
      const IconRect fittedCover = fitBookCoverRect(innerX, innerY, innerW, innerH);
      const RecentBook& b = recentBooks[static_cast<size_t>(bookIdx)];
      drawRecentCoverFitAt(fittedCover.x, fittedCover.y, fittedCover.w, fittedCover.h, b.cachePath, bookDisplayTitle(b),
                           ATKINSON_HYPERLEGIBLE_10_FONT_ID);
      drawProgressBadge(renderer, fittedCover, b.progress);
      const IconRect coverFrame = inflateIconRect(fittedCover, 5);
      if (selected) {
        renderThickIconRect(renderer, coverFrame, rr, 3);
      } else if (!rr) {
        renderer.rectangle.render(coverFrame.x, coverFrame.y, coverFrame.w, coverFrame.h, true, false);
      }
    }
  }
}
