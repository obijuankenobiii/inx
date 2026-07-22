void recent::SimpleUi::render(RecentActivity& self) { self.renderSimpleUi(); }

void RecentActivity::renderSimpleUi() {
  const int screenW = renderer.getScreenWidth();
  const SimpleUiMetrics m = computeSimpleUiMetrics(renderer);

  drawFlowCarouselBackdropInRect(renderer, 0, m.bodyTop, screenW, m.topBandH);

  const int recentSlots = recentBooks.empty() ? 0 : 1;
  if (recentSlots != 0) {
    const RecentBook& b = recentBooks[0];
    const int rx = m.marginL;
    const int ry = m.bodyTop + (m.topBandH - m.thumbH) / 2;
    const bool sel = (selectorIndex == 0);
    const bool rr = SETTINGS.bitmapRoundedCorners != 0;
    renderer.rectangle.fill(rx, ry, m.thumbW, m.thumbH, false, rr);
    const std::string cdir = b.cachePath.empty() ? epubCachePathForBookPath(b.path) : b.cachePath;
    drawRecentThumbnailAt(rx, ry, m.thumbW, m.thumbH, cdir, bookDisplayTitle(b), ATKINSON_HYPERLEGIBLE_12_FONT_ID,
                          true);
    if (sel) {
      renderer.rectangle.render(rx - 2, ry - 2, m.thumbW + 4, m.thumbH + 4, true, rr);
    } else if (!rr) {
      renderer.rectangle.render(rx, ry, m.thumbW, m.thumbH, true, false);
    }

    const int titleFont = kSimpleUiTitleFont;
    const int authorFont = kSimpleUiBodyFont;
    std::string titleStr = b.title.empty() ? formatTitle(getBaseFilename(b.path)) : b.title;
    const int textX = rx + m.thumbW + 18;
    const int maxTextW = std::max(40, screenW - textX - m.marginL);
    const std::string titleDraw = renderer.text.truncate(titleFont, titleStr.c_str(), maxTextW, EpdFontFamily::BOLD);
    const int lhTitle = renderer.text.getLineHeight(titleFont);
    const int lhAuthor = renderer.text.getLineHeight(authorFont);
    const int authorGap = 8;
    constexpr int kSimpleProgressBarH = 6;
    constexpr int kSimpleProgressBarGap = 12;
    const bool showProg = b.progress >= 0.0f && b.progress <= 1.0f;
    const int blockH = lhTitle + authorGap + lhAuthor + (showProg ? (kSimpleProgressBarGap + kSimpleProgressBarH) : 0);
    const int textY = m.bodyTop + (m.topBandH - blockH) / 2;
    renderer.text.render(titleFont, textX, textY, titleDraw.c_str(), true, EpdFontFamily::BOLD);
    const std::string auth = b.author.empty() ? std::string() : b.author;
    const std::string authDraw = renderer.text.truncate(authorFont, auth.c_str(), maxTextW);
    renderer.text.render(authorFont, textX, textY + lhTitle + authorGap, authDraw.c_str(), true);
    if (showProg) {
      const int barY = textY + lhTitle + authorGap + lhAuthor + kSimpleProgressBarGap;
      const int barW = maxTextW;
      renderer.rectangle.fill(textX, barY, barW, kSimpleProgressBarH, false);
      renderer.rectangle.render(textX, barY, barW, kSimpleProgressBarH, true);
      if (b.progress > 0.0f) {
        const int fillW = static_cast<int>(static_cast<float>(barW) * b.progress + 0.5f);
        renderer.rectangle.fill(textX, barY, fillW, kSimpleProgressBarH);
      }
    }
  } else {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, m.bodyTop + std::max(20, m.topBandH / 2 - 16),
                           "No recent books", true);
  }

  if (m.favTop < m.bodyBottom) {
    renderer.rectangle.fill(0, m.favTop, screenW, m.bodyBottom - m.favTop, false);
    renderer.line.render(0, m.favTop, screenW, m.favTop, true);
    const int favHdrFont = kSimpleUiBodyFont;
    renderer.text.render(favHdrFont, m.marginL, m.favTop + kFavHeaderPadTop, "Favorites", true, EpdFontFamily::BOLD);
    const int hdrSepY = m.favListTop - 1;
    if (hdrSepY > m.favTop) {
      renderer.line.render(0, hdrSepY, screenW, hdrSepY, true);
    }
  }

  const int favFont = kSimpleUiBodyFont;
  const int lh = renderer.text.getLineHeight(favFont);
  constexpr int kPadY = 18;
  clampSimpleUiFavoriteScroll(m.maxVis);

  const int fc = static_cast<int>(simpleUiFavorites_.size());

  if (fc == 0) {
    const int subFont = kSimpleUiLabelFont;
    const char* line1 = "No favorites yet.";
    const char* line2 = "Long press Confirm in Library to favorite books.";
    const int w1 = renderer.text.getWidth(favFont, line1);
    const int w2 = renderer.text.getWidth(subFont, line2);
    const int lh2 = renderer.text.getLineHeight(subFont);
    const int block = lh + 12 + lh2;
    const int paneH = m.bodyBottom - m.favListTop;
    const int y0 = m.favListTop + std::max(4, (paneH - block) / 2);
    renderer.text.render(favFont, (screenW - w1) / 2, y0, line1, true, EpdFontFamily::BOLD);
    renderer.text.render(subFont, (screenW - w2) / 2, y0 + lh + 12, line2, true);
    return;
  }

  int rowY = m.favListTop;
  const int endVi = std::min(fc, simpleUiFavScroll_ + m.maxVis);
  const int starX = m.marginL;
  const int titleX = starX + 34;
  for (int i = simpleUiFavScroll_; i < endVi; ++i) {
    const auto& fb = simpleUiFavorites_[static_cast<size_t>(i)];
    const int rowSelIndex = recentSlots + i;
    const bool rowSel = (selectorIndex == rowSelIndex);
    if (rowSel) {
      renderer.rectangle.fill(0, rowY, screenW, m.rowH, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    const int textY = rowY + kPadY;
    const int maxTitleW = std::max(40, screenW - titleX - m.marginL);
    std::string disp = fb.title.empty() ? formatTitle(getBaseFilename(fb.path)) : fb.title;
    const std::string trunc = renderer.text.truncate(favFont, disp.c_str(), maxTitleW);
    renderer.bitmap.icon(Star, starX, textY + 2, 24, 24, BitmapRender::Orientation::None, rowSel);
    renderer.text.render(favFont, titleX, textY, trunc.c_str(), !rowSel);
    rowY += m.rowH;
    renderer.line.render(0, rowY, screenW, rowY, true);
  }
}
