#include "EpubReadingStats.h"

#include <Arduino.h>
#include <Epub.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <cstdio>

#include "system/Fonts.h"

namespace {
constexpr unsigned long kStatsSaveIntervalMs = 30000;
constexpr uint32_t kReadingSessionCountMinMs = 45000;
}  // namespace

void EpubReadingStats::init(Epub& epub, const Section* section, const int currentSpineIndex) {
  readerSessionStartMs_ = millis();
  readingSessionCountCommitted_ = false;

  if (!loadBookStats(epub.getCachePath().c_str(), stats_)) {
    stats_.path = epub.getCachePath();
    stats_.title = epub.getTitle();
    stats_.author = epub.getAuthor();
    stats_.totalReadingTimeMs = 0;
    stats_.totalPagesRead = 0;
    stats_.totalChaptersRead = 0;
    stats_.lastReadTimeMs = millis();
    stats_.progressPercent = 0;
    stats_.lastSpineIndex = currentSpineIndex;
    stats_.lastPageNumber = section ? section->currentPage : 0;
    stats_.avgPageTimeMs = 0;
    stats_.sessionCount = 0;
  }

  stats_.lastReadTimeMs = millis();
  pageStartTime_ = millis();
  lastSaveTime_ = millis();
}

void EpubReadingStats::maybeCommitSession(Epub& epub) {
  if (readingSessionCountCommitted_) {
    return;
  }
  const uint32_t now = millis();
  if (now - readerSessionStartMs_ >= kReadingSessionCountMinMs) {
    stats_.sessionCount++;
    readingSessionCountCommitted_ = true;
    save(epub);
  }
}

void EpubReadingStats::startPageTimer() { pageStartTime_ = millis(); }

bool EpubReadingStats::hasActivePageTimer() const { return pageStartTime_ > 0; }

void EpubReadingStats::endPageTimer(Epub& epub, const Section* section, const int currentSpineIndex) {
  if (pageStartTime_ == 0) {
    return;
  }

  const uint32_t currentTime = millis();
  const uint32_t timeSpent = currentTime - pageStartTime_;

  if (timeSpent < 1000) {
    pageStartTime_ = 0;
    return;
  }

  if (section) {
    stats_.totalReadingTimeMs += timeSpent;
    stats_.totalPagesRead++;
    stats_.lastReadTimeMs = currentTime;
    stats_.lastSpineIndex = currentSpineIndex;
    stats_.lastPageNumber = section->currentPage;

    if (section->pageCount > 0) {
      const float spineProgress = static_cast<float>(section->currentPage) / section->pageCount;
      stats_.progressPercent = epub.calculateProgress(currentSpineIndex, spineProgress) * 100.0f;
    }

    if (stats_.totalPagesRead > 0) {
      stats_.avgPageTimeMs = stats_.totalReadingTimeMs / stats_.totalPagesRead;
    }

    const uint32_t now = millis();
    if (now - lastSaveTime_ >= kStatsSaveIntervalMs) {
      save(epub);
      lastSaveTime_ = now;
    }
  }

  pageStartTime_ = 0;
}

void EpubReadingStats::addChapterRead() { stats_.totalChaptersRead++; }

void EpubReadingStats::save(Epub& epub) {
  stats_.lastReadTimeMs = millis();
  ::saveBookStats(epub.getCachePath().c_str(), stats_);
}

void EpubReadingStats::display(GfxRenderer& renderer, Epub& epub) const {
  renderer.clearScreen(0xff);
  BookReadingStats stats;
  if (!loadBookStats(epub.getCachePath().c_str(), stats)) {
    return;
  }

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  constexpr int valueFont = ATKINSON_HYPERLEGIBLE_18_FONT_ID;
  constexpr int labelFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  const int statsX = (screenW - 250) / 2;
  const int statsY = (screenH - 300) / 2;
  int currentY = statsY;
  char buffer[32];

  renderer.text.render(ATKINSON_HYPERLEGIBLE_18_FONT_ID, statsX, statsY - 90, "End of book", true,
                       EpdFontFamily::BOLD);

  const std::string timeStr = formatTime(stats.totalReadingTimeMs);
  renderer.text.render(valueFont, statsX, currentY, timeStr.c_str(), true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Reading Time", true);
  currentY += 87;

  snprintf(buffer, sizeof(buffer), "%u", stats.totalPagesRead);
  renderer.text.render(valueFont, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Pages", true);
  currentY += 87;

  snprintf(buffer, sizeof(buffer), "%u", stats.totalChaptersRead);
  renderer.text.render(valueFont, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Chapters", true);
  currentY += 87;

  if (stats.avgPageTimeMs > 0) {
    snprintf(buffer, sizeof(buffer), "%us", stats.avgPageTimeMs / 1000);
  } else {
    snprintf(buffer, sizeof(buffer), "-");
  }
  renderer.text.render(valueFont, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Average / Page", true);

  currentY += 87;
  snprintf(buffer, sizeof(buffer), "%u", stats.sessionCount);
  renderer.text.render(valueFont, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Reading Sessions", true);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

std::string EpubReadingStats::formatTime(const uint32_t timeMs) {
  const uint32_t seconds = timeMs / 1000;
  const uint32_t minutes = seconds / 60;
  const uint32_t hours = minutes / 60;

  char buffer[32];
  if (hours > 0) {
    snprintf(buffer, sizeof(buffer), "%uh %um", hours, minutes % 60);
  } else if (minutes > 0) {
    snprintf(buffer, sizeof(buffer), "%um", minutes);
  } else {
    snprintf(buffer, sizeof(buffer), "%us", seconds);
  }
  return std::string(buffer);
}
