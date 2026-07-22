void recent::Flow::render(RecentActivity& self) { self.renderFlow(); }

void RecentActivity::renderFlow() {
  if (recentBooks.empty()) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, renderer.getScreenHeight() / 2, "No recent books");
    return;
  }

  const int screenW = renderer.getScreenWidth();
  const int startY = mainContentTop() + 5;

  int currentIndex = selectorIndex;
  int totalBooks = (int)recentBooks.size();

  int carouselW = screenW;
  int carouselH = 340;
  int carouselX = 0;
  int carouselY = startY;

  drawFlowCarouselBackdrop(renderer, carouselX, carouselY, carouselW, carouselH);

  const bool rr = SETTINGS.bitmapRoundedCorners != 0;

  int centerW = 210;
  int centerH = 318;
  int centerX = carouselX + (carouselW - centerW) / 2;
  int centerY = carouselY + (carouselH - centerH) / 2 + 4;

  float scale = 0.9f;
  int sideW = (int)(centerW * scale);
  int sideH = (int)(centerH * scale);
  int leftX = centerX - sideW - 20;
  int rightX = centerX + centerW + 20;
  int sideY = centerY + (centerH - sideH) / 2;

  if (currentIndex > 0) {
    const RecentBook& leftBook = recentBooks[currentIndex - 1];
    renderer.rectangle.fill(leftX, sideY, sideW, sideH, false, rr);
    drawRecentThumbnailAt(leftX, sideY, sideW, sideH, leftBook.cachePath, bookDisplayTitle(leftBook),
                          ATKINSON_HYPERLEGIBLE_10_FONT_ID, true);
  }

  if (currentIndex + 1 < totalBooks) {
    const RecentBook& rightBook = recentBooks[currentIndex + 1];
    renderer.rectangle.fill(rightX, sideY, sideW, sideH, false, rr);
    drawRecentThumbnailAt(rightX, sideY, sideW, sideH, rightBook.cachePath, bookDisplayTitle(rightBook),
                          ATKINSON_HYPERLEGIBLE_10_FONT_ID, true);
  }

  const RecentBook& currentBook = recentBooks[currentIndex];

  renderer.rectangle.fill(centerX, centerY, centerW, centerH, false, rr);
  drawRecentThumbnailAt(centerX, centerY, centerW, centerH, currentBook.cachePath, bookDisplayTitle(currentBook),
                        ATKINSON_HYPERLEGIBLE_14_FONT_ID, true);

  const CachedRecentStats& cachedStats = statsForRecentIndex(currentIndex);
  const BookReadingStats& stats = cachedStats.stats;
  const bool hasStats = cachedStats.loaded;

  const int VALUE_FONT = ATKINSON_HYPERLEGIBLE_16_FONT_ID;
  const int LABEL_FONT = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  int statsX = 30;
  int statsY = carouselY + carouselH + 25;
  renderer.line.render(0, carouselY + carouselH + 10, screenW, carouselY + carouselH + 10, true);
  std::string title;
  if (!currentBook.title.empty()) {
    title = currentBook.title;
  } else {
    title = formatTitle(getBaseFilename(currentBook.path));
  }
  std::string truncatedTitle =
      renderer.text.truncate(ATKINSON_HYPERLEGIBLE_18_FONT_ID, title.c_str(), screenW - 60, EpdFontFamily::BOLD);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_18_FONT_ID, statsX, statsY, truncatedTitle.c_str(), true,
                       EpdFontFamily::BOLD);

  int authorY = statsY + renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_18_FONT_ID) - 5;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, statsX, authorY, currentBook.author.c_str());

  float progress = hasStats ? stats.progressPercent : (currentBook.progress * 100.0f);
  if (progress >= 0) {
    int barY = authorY + renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID) + 20;
    int barW = (screenW - 60) * 0.5;
    int barH = 6;

    renderer.rectangle.fill(statsX, barY, barW, barH, false);
    renderer.rectangle.render(statsX, barY, barW, barH, true);
    if (progress > 0) {
      int fillW = (int)(barW * (progress / 100.0f));
      renderer.rectangle.fill(statsX, barY, fillW, barH);
    }

    char percentText[8];
    int percent = (int)(progress + 0.5f);
    snprintf(percentText, sizeof(percentText), "%d%%", percent);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, statsX + barW + 12, barY - 13, percentText);
  }

  if (hasStats) {
    char buffer[32];

    int gridStartY = authorY + 100;
    int col1X = statsX;
    int col2X = (screenW) / 2;
    int rowHeight = 95;

    std::string timeStr = formatTime(stats.totalReadingTimeMs);
    renderer.text.render(VALUE_FONT, col1X, gridStartY, timeStr.c_str(), true, EpdFontFamily::BOLD);
    renderer.text.render(LABEL_FONT, col1X, gridStartY + 40, "Reading Time", true);

    snprintf(buffer, sizeof(buffer), "%u", stats.totalPagesRead);
    renderer.text.render(VALUE_FONT, col2X, gridStartY, buffer, true, EpdFontFamily::BOLD);
    renderer.text.render(LABEL_FONT, col2X, gridStartY + 40, "Pages", true);

    int row2Y = gridStartY + rowHeight;

    snprintf(buffer, sizeof(buffer), "%u", stats.totalChaptersRead);
    renderer.text.render(VALUE_FONT, col1X, row2Y, buffer, true, EpdFontFamily::BOLD);
    renderer.text.render(LABEL_FONT, col1X, row2Y + 40, "Chapters", true);

    uint32_t avgPageTime = stats.avgPageTimeMs;
    if (avgPageTime > 0) {
      snprintf(buffer, sizeof(buffer), "%u s", avgPageTime / 1000);
    } else {
      snprintf(buffer, sizeof(buffer), "-");
    }
    renderer.text.render(VALUE_FONT, col2X, row2Y, buffer, true, EpdFontFamily::BOLD);
    renderer.text.render(LABEL_FONT, col2X, row2Y + 40, "Average / Page", true);
  }
}
