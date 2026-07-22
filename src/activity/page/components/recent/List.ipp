void recent::List::render(RecentActivity& self, int startY) { self.renderList(startY); }

void RecentActivity::renderList(int startY) {
  const int totalBooks = static_cast<int>(recentBooks.size());
  if (totalBooks == 0) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, startY + 150, "No recent books");
    return;
  }

  constexpr int kHintReserve = 54;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int contentBottom = screenH - kHintReserve;
  const int contentH = std::max(1, contentBottom - startY);
  const int rowH = std::max(56, contentH / LIST_VISIBLE_ITEMS);
  constexpr int padX = 30;
  const int thumbH = std::max(48, rowH - 10);
  const int thumbW = std::min(88, thumbH * RecentActivity::COVER_WIDTH / RecentActivity::COVER_HEIGHT);
  const bool thumbRound = SETTINGS.bitmapRoundedCorners != 0;

  const int visibleCount = std::min(LIST_VISIBLE_ITEMS, totalBooks - scrollOffset);
  for (int slot = 0; slot < visibleCount; ++slot) {
    const int bi = scrollOffset + slot;
    const int y = startY + slot * rowH;
    const RecentBook& book = recentBooks[static_cast<size_t>(bi)];
    const bool selected = !suppressBufferedSelection_ && (selectorIndex == bi);

    if (selected) {
      renderer.rectangle.render(padX / 2, y + 1, screenW - padX, rowH, true, false);
    }

    const int ty = y + (rowH - thumbH) / 2;
    const int tx = padX;
    const std::string cacheDir = book.cachePath.empty() ? epubCachePathForBookPath(book.path) : book.cachePath;
    if (thumbRound) {
      renderer.rectangle.fill(tx, ty, thumbW, thumbH, false, true);
    }
    drawRecentThumbnailAt(tx, ty, thumbW, thumbH, cacheDir, bookDisplayTitle(book), ATKINSON_HYPERLEGIBLE_10_FONT_ID,
                          false);

    const int textX = tx + thumbW + 14;
    const int textRight = screenW - padX;
    const int textW = std::max(40, textRight - textX);

    const int fontTitle = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
    const int fontAuthor = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
    const int lhT = renderer.text.getLineHeight(fontTitle);
    const int lhA = renderer.text.getLineHeight(fontAuthor);
    const int tyT = y + 20;
    const std::string dispTitle = bookDisplayTitle(book);
    const std::string titleLine = renderer.text.truncate(fontTitle, dispTitle.c_str(), textW, EpdFontFamily::REGULAR);
    renderer.text.render(fontTitle, textX, tyT, titleLine.c_str(), true, EpdFontFamily::REGULAR);
    int lastTextBottom = tyT + lhT;
    int tyA = tyT + lhT + 4;
    if (!book.author.empty()) {
      const std::string auth = renderer.text.truncate(fontAuthor, book.author.c_str(), textW);
      renderer.text.render(fontAuthor, textX, tyA, auth.c_str());
      lastTextBottom = tyA + lhA;
    }

    float prog = book.progress;
    if (prog < 0.f || prog > 1.f) {
      prog = 0.f;
    }
    char pctBuf[12];
    snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", static_cast<double>(prog * 100.f));
    const int fontPct = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
    const int pctW = renderer.text.getWidth(fontPct, pctBuf);
    constexpr int barH = 8;
    int barY = lastTextBottom + 20;
    barY = std::max(barY, tyT + lhT + 4);
    barY = std::min(barY, y + rowH - barH - 4);
    if (barY < tyT + lhT) {
      barY = y + rowH - barH - 4;
    }
    const int barX = textX;
    const int barW = std::max(24, textRight - pctW - 10 - barX);

    renderer.rectangle.fill(barX, barY, barW, barH, false);
    renderer.rectangle.render(barX, barY, barW, barH, true);
    const int fillW = static_cast<int>(static_cast<float>(barW) * prog + 0.5f);
    if (fillW > 0) {
      renderer.rectangle.fill(barX, barY, fillW, barH, true);
    }
    renderer.text.render(fontPct, barX + barW + 6, barY - 1, pctBuf, false);

    const bool isLastVisibleRow = (slot == visibleCount - 1);
    if (!isLastVisibleRow) {
      const int lineY = y + rowH - 1;
      const int x0 = padX / 2;
      const int x1 = screenW - padX / 2;
      for (int px = x0; px < x1; px += 3) {
        renderer.drawPixel(px, lineY, true);
      }
    }
  }
}
