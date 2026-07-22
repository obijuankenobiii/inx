void recent::Cover::render(RecentActivity& self) { self.renderCoverMode(); }

void RecentActivity::renderCoverMode() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int bodyTop = mainContentTop() + 6;
  const int bodyBottom = INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) : screenH - 36;
  const int bodyH = std::max(1, bodyBottom - bodyTop);

  if (recentBooks.empty()) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, bodyTop + bodyH / 2 - 8, "No recent books", true);
    return;
  }

  if (selectorIndex < 0) {
    selectorIndex = 0;
  }
  const int totalBooks = static_cast<int>(recentBooks.size());
  if (selectorIndex >= totalBooks) {
    selectorIndex = totalBooks - 1;
  }
  const RecentBook& b = recentBooks[static_cast<size_t>(selectorIndex)];

  constexpr int kProgressGap = 10;
  constexpr int kProgressH = 8;
  constexpr int kMinBottomPad = 12;
  const bool showProgress = b.progress >= 0.0f && b.progress <= 1.0f;
  const int progressBlockH = showProgress ? (kProgressGap + kProgressH) : 0;

  int coverW = std::max(80, screenW * 78 / 100);
  const int maxCoverW = std::max(80, screenW - GRID_SPACING * 2);
  coverW = std::min(coverW, maxCoverW);
  int coverH = coverW * COVER_HEIGHT / COVER_WIDTH;
  const int maxCoverH = std::max(90, bodyH - progressBlockH - kMinBottomPad);
  if (coverH > maxCoverH) {
    coverH = maxCoverH;
    coverW = std::max(80, coverH * COVER_WIDTH / COVER_HEIGHT);
  }

  const int coverX = (screenW - coverW) / 2;
  const int coverY = bodyTop + std::max(0, (bodyH - coverH - progressBlockH) / 2);
  const bool rr = SETTINGS.bitmapRoundedCorners != 0;
  renderer.rectangle.fill(coverX, coverY, coverW, coverH, false, rr);
  const std::string cdir = b.cachePath.empty() ? epubCachePathForBookPath(b.path) : b.cachePath;
  const std::string coverPath = resolveCoverPath(cdir);
  bool coverDrawn = false;
  if (!coverPath.empty()) {
    ImageRender::Options options;
    options.cropToFill = true;
    options.useDisplayCache = true;
    if (SETTINGS.bitmapRoundedCorners == 0) {
      options.roundedOutside = BitmapRender::RoundedOutside::None;
    } else if (SETTINGS.bitmapRoundedCorners == 2) {
      options.roundedOutside = BitmapRender::RoundedOutside::SubtlePaperOutside;
    } else {
      options.roundedOutside = BitmapRender::RoundedOutside::PaperOutside;
    }
    const ImageRender image = ImageRender::create(renderer, coverPath);
    coverDrawn = image.renderDisplayCacheOnly(coverX, coverY, coverW, coverH, options);
    if (!coverDrawn && queueRecentImageCacheBuild(coverPath, coverX, coverY, coverW, coverH, options.cropToFill,
                                                  options.roundedOutside)) {
      drawRecentThumbnailPlaceholder(renderer, coverX, coverY, coverW, coverH, true);
      coverDrawn = true;
    }
  }
  if (!coverDrawn) {
    drawRecentNoCoverPlaceholder(renderer, coverX, coverY, coverW, coverH, bookDisplayTitle(b),
                                 ATKINSON_HYPERLEGIBLE_14_FONT_ID);
  }
  renderer.rectangle.render(coverX - 2, coverY - 2, coverW + 4, coverH + 4, true, rr);

  if (showProgress) {
    const int barY = std::min(coverY + coverH + kProgressGap, bodyBottom - kProgressH - kMinBottomPad);
    const int barW = std::max(24, coverW * 80 / 100);
    const int barX = coverX + (coverW - barW) / 2;
    renderer.rectangle.fill(barX, barY, barW, kProgressH, false);
    renderer.rectangle.render(barX, barY, barW, kProgressH, true);
    if (b.progress > 0.0f) {
      const int innerW = std::max(0, barW - 2);
      const int innerH = std::max(0, kProgressH - 2);
      const int fillW = static_cast<int>(static_cast<float>(innerW) * b.progress + 0.5f);
      if (fillW > 0) {
        renderer.rectangle.fill(barX + 1, barY + 1, fillW, innerH);
      }
    }
  }
}
