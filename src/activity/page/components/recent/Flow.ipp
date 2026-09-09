void recent::Flow::render(RecentActivity& self) { self.renderFlow(); }

void RecentActivity::renderFlow() {
  if (recentBooks.empty()) {
    renderer.text.centered(MONTSERRAT_12_FONT_ID, renderer.getScreenHeight() / 2, "No recent books");
    return;
  }

  const int screenW = renderer.getScreenWidth();
  const int startY = mainContentTop() + 5;

  const int currentIndex = std::max(0, std::min(selectorIndex, static_cast<int>(recentBooks.size()) - 1));
  const int carouselY = startY;
  widget::Carousel carousel(renderer);
  carousel.render(recentBooks, currentIndex, 0, carouselY, screenW, widget::Carousel::kHeight,
                  &RecentActivity::renderFlowThumbnail, this);

  const RecentBook& currentBook = recentBooks[static_cast<size_t>(currentIndex)];

  const CachedRecentStats& cachedStats = statsForRecentIndex(currentIndex);
  const BookReadingStats& stats = cachedStats.stats;
  const bool hasStats = cachedStats.loaded;

  const int VALUE_FONT = MONTSERRAT_16_FONT_ID;
  const int LABEL_FONT = MONTSERRAT_10_FONT_ID;

  int statsX = 30;
  int statsY = carouselY + widget::Carousel::kHeight + 25;
  renderer.line.render(0, carouselY + widget::Carousel::kHeight + 10, screenW,
                       carouselY + widget::Carousel::kHeight + 10, true);
  std::string title;
  if (!currentBook.title.empty()) {
    title = currentBook.title;
  } else {
    title = formatTitle(getBaseFilename(currentBook.path));
  }
  std::string truncatedTitle =
      renderer.text.truncate(MONTSERRAT_18_FONT_ID, title.c_str(), screenW - 60, EpdFontFamily::BOLD);
  renderer.text.render(MONTSERRAT_18_FONT_ID, statsX, statsY, truncatedTitle.c_str(), true,
                       EpdFontFamily::BOLD);

  int authorY = statsY + renderer.text.getLineHeight(MONTSERRAT_18_FONT_ID) - 5;
  renderer.text.render(MONTSERRAT_12_FONT_ID, statsX, authorY, currentBook.author.c_str());

  float progress = hasStats ? stats.progressPercent : (currentBook.progress * 100.0f);
  if (progress >= 0) {
    int barY = authorY + renderer.text.getLineHeight(MONTSERRAT_12_FONT_ID) + 20;
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
    renderer.text.render(MONTSERRAT_12_FONT_ID, statsX + barW + 12, barY - 13, percentText);
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
