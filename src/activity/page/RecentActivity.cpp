#include "RecentActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <algorithm>
#include <memory>
#include <sstream>

#include "images/Down.h"
#include "images/Tree.h"
#include "images/Up.h"
#include "state/BookState.h"
#include "state/Statistics.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

namespace {

/**
 * Extracts the base filename from a full path.
 * Removes directory path and file extension, then replaces underscores with spaces.
 */
static std::string getBaseFilename(const std::string& filename) {
  size_t lastSlash = filename.find_last_of('/');
  std::string basename = (lastSlash != std::string::npos) ? filename.substr(lastSlash + 1) : filename;
  size_t lastDot = basename.find_last_of('.');
  if (lastDot != std::string::npos) {
    basename.resize(lastDot);
  }
  std::replace(basename.begin(), basename.end(), '_', ' ');
  return basename;
}

/**
 * Formats a title by capitalizing the first letter of each word.
 */
static std::string formatTitle(const std::string& title) {
  std::string formatted = title;
  bool capitalizeNext = true;
  for (char& c : formatted) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      capitalizeNext = true;
    } else if (capitalizeNext) {
      c = std::toupper(static_cast<unsigned char>(c));
      capitalizeNext = false;
    }
  }
  return formatted;
}

/**
 * RAII wrapper for mutex operations with timeout.
 */
class MutexGuard {
 private:
  SemaphoreHandle_t& mutex;
  bool acquired;

 public:
  explicit MutexGuard(SemaphoreHandle_t& m) : mutex(m), acquired(false) {
    if (mutex) {
      acquired = (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE);
    }
  }

  ~MutexGuard() {
    if (acquired && mutex) {
      xSemaphoreGive(mutex);
    }
  }

  bool isAcquired() const { return acquired; }
};

constexpr unsigned long GO_HOME_MS = 1000;

}  // namespace

/**
 * Calculates the number of rows that can be displayed on screen at once.
 */
int RecentActivity::getVisibleRows() const {
  if (currentViewMode == ViewMode::Grid) {
    int screenHeight = renderer.getScreenHeight();
    int availableHeight = screenHeight - TAB_BAR_HEIGHT - 20;
    return (availableHeight > 0) ? availableHeight / GRID_ITEM_HEIGHT : 1;
  }
  return LIST_VISIBLE_ITEMS;
}

/**
 * Loads recent books from persistent storage.
 */
void RecentActivity::loadRecentBooks() {
  recentBooks.clear();
  recentBooks.reserve(MAX_RECENT_BOOKS);
  scrollOffset = 0;

  const auto& allBooks = RECENT_BOOKS.getBooks();
  size_t addedCount = 0;

  for (size_t i = 0; i < allBooks.size() && addedCount < MAX_RECENT_BOOKS; ++i) {
    const auto& book = allBooks[i];
    if (!SdMan.exists(book.path.c_str())) {
      continue;
    }
    recentBooks.push_back(book);
    addedCount++;
  }
}

/**
 * Static trampoline function for FreeRTOS task creation.
 */
void RecentActivity::taskTrampoline(void* param) { static_cast<RecentActivity*>(param)->displayTaskLoop(); }

/**
 * Initializes the recent activity when entered.
 */
void RecentActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  if (!renderingMutex) return;
  renderer.clearScreen(0xff);
  loadRecentBooks();

  currentViewMode = (SETTINGS.recentLibraryMode == SystemSetting::RECENT_GRID) ? ViewMode::Grid : ViewMode::Default;
  if (currentViewMode == ViewMode::Default) {
    renderDefault();
  } else {
    renderGrid(TAB_BAR_HEIGHT - 20);
  }

  firstRender = false;

  if (displayTaskHandle == nullptr) {
    xTaskCreate(&RecentActivity::taskTrampoline, "RecentTask", 4096, this, 1, &displayTaskHandle);
  }

  updateRequired = true;
}

/**
 * Cleans up resources when exiting the recent activity.
 */
void RecentActivity::onExit() {
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }

  recentBooks.clear();
  Activity::onExit();
}

/**
 * Renders the complete grid view including all visible books.
 */
void RecentActivity::renderGrid(int startY) {
  renderTabBar(renderer);
  int totalBooks = static_cast<int>(recentBooks.size());
  if (totalBooks == 0) {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, startY + 150, "No recent books");
    return;
  }

  int visibleRows = getVisibleRows();
  int startRow = scrollOffset;
  int endRow = std::min(startRow + visibleRows, (totalBooks + GRID_COLS - 1) / GRID_COLS);
  for (int row = startRow; row < endRow; ++row) {
    for (int col = 0; col < GRID_COLS; ++col) {
      int bookIdx = row * GRID_COLS + col;
      if (bookIdx >= totalBooks) return;
      bool isSelected = (selectorIndex == bookIdx);
      renderGridItem(col, row - startRow, startY, recentBooks[bookIdx], isSelected);
    }
  }
}

/**
 * Renders a single grid item including cover image or placeholder.
 */
void RecentActivity::renderGridItem(int gridX, int gridY, int startY, const RecentBook& book, bool selected) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight() - 20;

  int availableWidth = screenW - (GRID_COLS + 1) * GRID_SPACING;
  int containerWidth = availableWidth / GRID_COLS;

  int visibleRows = getVisibleRows();
  int availableHeight = screenH - startY - (GRID_SPACING * 2);
  int containerHeight = (availableHeight / visibleRows) - GRID_SPACING;

  int itemX = GRID_SPACING + gridX * (containerWidth + GRID_SPACING);
  int itemY = startY + GRID_SPACING + gridY * (containerHeight + GRID_SPACING);

  if (selected) {
    for (int y = itemY + 10; y < itemY + GRID_ITEM_HEIGHT + 63; y += 2) {
      for (int x = itemX - 12; x < itemX + containerWidth + 12; x += 2) {
        renderer.drawPixel(x, y, true);
      }
    }
  }

  int coverAreaX = itemX;
  int coverAreaY = itemY;
  int coverHeight = static_cast<int>((containerHeight));

  bool coverDrawn = false;

  if (!book.cachePath.empty()) {
    char thumbPath[128];
    snprintf(thumbPath, sizeof(thumbPath), "%s/thumb.bmp", book.cachePath.c_str());

    FsFile file;
    if (SdMan.openFileForRead("RECENT", thumbPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        int bw = bitmap.getWidth();
        int bh = bitmap.getHeight();

        int scaledW = bw * 98 / 100;
        int scaledH = bh * 98 / 100;

        int drawX = coverAreaX + (containerWidth - scaledW) / 2;
        int drawY = coverAreaY + (coverHeight - scaledH) / 2 + GRID_SPACING;
        renderer.drawSmallBitmapClean(bitmap, drawX, drawY, scaledW, scaledH);
        coverDrawn = true;
      }
      file.close();
    }
  }

  if (!coverDrawn) {
    int bookX = coverAreaX ;
    int bookY = coverAreaY + GRID_SPACING;
    int bookWidth = containerWidth;
    int bookHeightInt = static_cast<int>(containerHeight);

    renderer.drawRect(bookX, bookY, bookWidth, bookHeightInt, true);
    renderer.fillRect(bookX, bookY, bookWidth, bookHeightInt);
    renderer.drawIcon(Tree, bookX, bookY + 100, 200, 200, GfxRenderer::Rotate270CW, true);
    char titleBuf[128];
    if (!book.title.empty()) {
      strncpy(titleBuf, book.title.c_str(), sizeof(titleBuf));
    } else {
      std::string tmp = formatTitle(getBaseFilename(book.path));
      strncpy(titleBuf, tmp.c_str(), sizeof(titleBuf));
    }
    titleBuf[sizeof(titleBuf) - 1] = '\0';

    char* words[8];
    int wordCount = 0;
    char* token = strtok(titleBuf, " ");
    while (token && wordCount < 8) {
      words[wordCount++] = token;
      token = strtok(nullptr, " ");
    }

    int lineY = bookY + 20;
    int leftMargin = bookX + 10;

    for (int i = 0; i < wordCount && i < 8; i += 2) {
      char line[64];
      if (i + 1 < wordCount) {
        snprintf(line, sizeof(line), "%s %s", words[i], words[i + 1]);
      } else {
        snprintf(line, sizeof(line), "%s", words[i]);
      }
      renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, leftMargin, lineY, line, false, EpdFontFamily::BOLD);
      lineY += renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID);
    }

    if (!book.author.empty()) {
      char authorBuf[64];
      snprintf(authorBuf, sizeof(authorBuf), "- %s", book.author.c_str());
      int authorY = bookY + bookHeightInt;
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, leftMargin, authorY, authorBuf, false);
    }
  }

  char titleBuf[128];
  if (!book.title.empty()) {
    strncpy(titleBuf, book.title.c_str(), sizeof(titleBuf));
  } else {
    std::string tmp = formatTitle(getBaseFilename(book.path));
    strncpy(titleBuf, tmp.c_str(), sizeof(titleBuf));
  }
  titleBuf[sizeof(titleBuf) - 1] = '\0';

  if (book.progress >= 0.0f && book.progress <= 1.0f) {
    int barX = coverAreaX + 15;
    int barY = coverAreaY + containerHeight;
    int barW = containerWidth - 30;
    int barH = 10;

    renderer.fillRect(barX, barY, barW, barH, false);
    renderer.drawRect(barX, barY, barW, barH, true);

    if (book.progress > 0.0f) {
      int fillW = static_cast<int>(barW * book.progress + 0.5f);
      renderer.fillRect(barX, barY, fillW, barH);

      char pText[8];
      int percent = static_cast<int>(book.progress * 100.0f + 0.5f);
      snprintf(pText, sizeof(pText), "%d%%", percent);
      int pW = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, pText);
      renderer.fillRect(barX + barW - pW - 5, barY - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_8_FONT_ID) - 10,
                        pW + 5, 30, false, true);
      renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, barX + barW - pW,
                        barY - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_8_FONT_ID) - 6, pText);
    }
  }
}

/**
 * Display task loop that runs in a separate FreeRTOS task.
 */
void RecentActivity::displayTaskLoop() {
  while (true) {
    {
      MutexGuard guard(renderingMutex);
      if (guard.isAcquired() && updateRequired) {
        renderer.clearScreen();
        renderTabBar(renderer);

        if (currentViewMode == ViewMode::Default) {
          renderDefault();
        } else {
          renderGrid(TAB_BAR_HEIGHT - 29);
        }

        renderer.displayBuffer();
        updateRequired = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
/**
 * Renders a single list item with thumbnail and details.
 */
void RecentActivity::renderListItem(int index, int startY, const RecentBook& book, bool selected, bool next) {
  const int LIST_ITEM_HEIGHT = (renderer.getScreenHeight() - TAB_BAR_HEIGHT - 85) / 5 + 10;
  const int LIST_ITEM_WIDTH = renderer.getScreenWidth();
  const int LIST_START_X = 0;
  const int ITEM_SPACING = 10;

  int itemY = startY + index * (LIST_ITEM_HEIGHT + ITEM_SPACING);

  if (itemY < 0 || itemY >= renderer.getScreenHeight()) {
    return;
  }

  if (selected) {
    for (int y = itemY - ITEM_SPACING; y < itemY + LIST_ITEM_HEIGHT; y += 2) {
      if (y < 0 || y >= renderer.getScreenHeight()) continue;
      for (int x = LIST_START_X; x < LIST_ITEM_WIDTH; x += 2) {
        if (x >= 0 && x < renderer.getScreenWidth()) {
          renderer.drawPixel(x, y, true);
        }
      }
    }
  }

  int thumbX = LIST_START_X + 10;
  int thumbY = itemY + 10;
  int thumbWidth = 120;
  int thumbHeight = 132;

  int textX = thumbX + thumbWidth + 15;
  int textY = itemY + 18;
  int textWidth = LIST_ITEM_WIDTH - thumbWidth - 60;

  bool coverDrawn = false;

  if (!book.cachePath.empty()) {
    char smThumbPath[128];
    snprintf(smThumbPath, sizeof(smThumbPath), "%s/thumb.bmp", book.cachePath.c_str());

    FsFile file;
    if (SdMan.openFileForRead("RECENT", smThumbPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        int bitmapWidth = bitmap.getWidth();
        int bitmapHeight = bitmap.getHeight();
        float bitmapRatio = static_cast<float>(bitmapWidth) / bitmapHeight;

        int drawWidth = thumbWidth;
        int drawHeight = static_cast<int>(drawWidth / bitmapRatio);

        if (drawHeight > thumbHeight) {
          drawHeight = thumbHeight;
          drawWidth = static_cast<int>(drawHeight * bitmapRatio);
        }

        int drawX = thumbX + (thumbWidth - drawWidth) / 2;
        int drawY = thumbY + (thumbHeight - drawHeight) / 2;

        if (drawX >= 0 && drawX < renderer.getScreenWidth() && drawY >= 0 && drawY < renderer.getScreenHeight()) {
          renderer.drawRect(drawX, drawY - 12, drawWidth, drawHeight);
          renderer.drawSmallBitmapClean(bitmap, drawX, drawY - 12, drawWidth, drawHeight);
          coverDrawn = true;
        }
      }
      file.close();
    }
  }

  if (!coverDrawn && !next) {
    int fillX = thumbX + 10;
    int fillY = thumbY - 10;
    if (fillX >= 0 && fillX < renderer.getScreenWidth()) {
      renderer.fillRect(fillX, fillY, thumbWidth - 20, thumbHeight, true);
    }
  }

  std::string titleString;
  if (book.title.empty()) {
    titleString = formatTitle(getBaseFilename(book.path));
  } else {
    titleString = book.title;
  }

  if (textX >= 0 && textX < renderer.getScreenWidth()) {
    std::string truncatedTitle =
        renderer.truncatedText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, titleString.c_str(), textWidth, EpdFontFamily::BOLD);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, textX, textY, truncatedTitle.c_str(), true,
                      EpdFontFamily::BOLD);
  }

  int authorY = textY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID);
  if (authorY >= 0 && authorY < renderer.getScreenHeight()) {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, authorY, book.author.c_str());
  }

  if (book.progress >= 0.0f && book.progress <= 1.0f && !next && book.progress < 0.99f) {
    int progressBarY = itemY + LIST_ITEM_HEIGHT - 40;

    if (progressBarY >= 0 && progressBarY < renderer.getScreenHeight()) {
      int progressBarX = textX;
      int progressBarWidth = static_cast<int>(textWidth);
      int progressBarHeight = 8;

      renderer.fillRect(progressBarX, progressBarY, progressBarWidth, progressBarHeight, false);
      renderer.drawRect(progressBarX, progressBarY, progressBarWidth, progressBarHeight, true);

      if (book.progress > 0.0f) {
        int fillWidth = static_cast<int>(progressBarWidth * book.progress + 0.5f);
        renderer.fillRect(progressBarX, progressBarY, fillWidth, progressBarHeight);
      }
      char progressText[8];
      int percent = static_cast<int>(book.progress * 100.0f + 0.5f);
      int len = snprintf(progressText, sizeof(progressText), "%d%%", percent);
      if (len > 0) {
        int textW = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, progressText);
        int percentX = progressBarX + progressBarWidth - textW - 5;
        int percentY = progressBarY - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) - 2;
        if (percentX >= 0 && percentY >= 0) {
          renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, percentX, percentY, progressText);
        }
      }
    }
  } else if (book.progress >= 0.99f && !next) {
    int completedY = itemY + LIST_ITEM_HEIGHT - 40;
    if (completedY >= 0 && completedY < renderer.getScreenHeight()) {
      const char* completedText = "Completed";
      int textW = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, completedText);
      int completedX = textX + textWidth - textW - 5;
      if (completedX >= 0 && completedX < renderer.getScreenWidth()) {
        renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, completedX, completedY, completedText);
      }
    }
  }
}

/**
 * Formats milliseconds into a human-readable time string.
 * Output format: "X.X h" for hours (with one decimal), "X m" for minutes.
 */
std::string RecentActivity::formatTime(uint32_t milliseconds) const {
  char buffer[32];
  float hours = milliseconds / (1000.0f * 3600.0f);
  
  if (hours >= 1.0f) {
    snprintf(buffer, sizeof(buffer), "%.1f h", hours);
  } else {
    uint32_t minutes = milliseconds / (1000 * 60);
    snprintf(buffer, sizeof(buffer), "%u m", minutes);
  }
  return std::string(buffer);
}

/**
 * Renders the default view with current book stats and iterating through remaining recent books.
 */
void RecentActivity::renderDefault() {
  if (recentBooks.empty()) {
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, renderer.getScreenHeight() / 2, "No recent books");
    return;
  }

  const RecentBook& currentBook = recentBooks[0];

  BookReadingStats stats;
  bool hasStats = false;
  if (!currentBook.cachePath.empty()) {
    hasStats = loadBookStats(currentBook.cachePath.c_str(), stats);
  }

  BookReadingStats secondStats;
  bool hasSecondStats = false;
  if (recentBooks.size() > 1 && !recentBooks[1].cachePath.empty()) {
    hasSecondStats = loadBookStats(recentBooks[1].cachePath.c_str(), secondStats);
  }

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int startY = TAB_BAR_HEIGHT - 6;
  const int VALUE_FONT = ATKINSON_HYPERLEGIBLE_18_FONT_ID;
  const int LABEL_FONT = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  int availableWidth = screenW - (2 + 1) * GRID_SPACING;
  int containerWidth = availableWidth / 2 + 18;
  int containerHeight = 360;

  int coverItemX = GRID_SPACING;
  int coverItemY = startY + GRID_SPACING;
  int detailsItemX = GRID_SPACING + containerWidth + 10;
  int detailsItemY = coverItemY;

  int coverAreaX = coverItemX;
  int coverAreaY = coverItemY + 45;
  int coverWidth = containerWidth - 10;
  int coverHeight = static_cast<int>((containerHeight - 40) * 0.75);

  bool coverDrawn = false;

  int borderX = 0;
  int borderY = coverItemY - 10;
  int borderWidth = screenW;
  int borderHeight = containerHeight + 40;

  for (int x = borderX; x <= borderX + borderWidth; x += 2) {
    if (x >= 0 && x < renderer.getScreenWidth()) {
      renderer.drawPixel(x, borderY + borderHeight, true);
    }
  }

  if (!currentBook.cachePath.empty()) {
    char thumbPath[128];
    snprintf(thumbPath, sizeof(thumbPath), "%s/thumb.bmp", currentBook.cachePath.c_str());

    FsFile file;
    if (SdMan.openFileForRead("RECENT", thumbPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        int bw = bitmap.getWidth() * 45/100;
        int bh = bitmap.getHeight()* 45/100;
        int drawX = coverAreaX + (coverWidth - bw) / 2;
        int drawY = coverAreaY + (coverHeight - bh) / 2;
        renderer.drawSmallBitmapClean(bitmap, drawX, drawY, bw, bh);
        coverDrawn = true;
      }
      file.close();
    }
  }

  if (!coverDrawn) {
    int bookX = coverAreaX - 5;
    int bookY = coverAreaY - 40;
    int bookWidth = containerWidth - 10;
    int bookHeight = static_cast<int>(containerHeight - 30);
    bool black = false;
    renderer.drawRect(bookX, bookY, bookWidth, bookHeight, !black);
    renderer.drawIcon(Tree, (bookWidth - 170) / 2, (bookHeight / 2) - 50, 200, 200, GfxRenderer::Rotate270CW, black);
    char titleBuf[128];
    if (!currentBook.title.empty()) {
      strncpy(titleBuf, currentBook.title.c_str(), sizeof(titleBuf));
    } else {
      std::string tmp = formatTitle(getBaseFilename(currentBook.path));
      strncpy(titleBuf, tmp.c_str(), sizeof(titleBuf));
    }
    titleBuf[sizeof(titleBuf) - 1] = '\0';

    char* words[4];
    int wordCount = 0;
    char* token = strtok(titleBuf, " ");
    while (token && wordCount < 4) {
      words[wordCount++] = token;
      token = strtok(nullptr, " ");
    }

    int lineY = bookY + bookHeight - 110;
    int leftMargin = bookX + 10;

    for (int i = 0; i < wordCount && i < 8; i += 2) {
      char line[64];
      if (i + 1 < wordCount) {
        snprintf(line, sizeof(line), "%s %s", words[i], words[i + 1]);
      } else {
        snprintf(line, sizeof(line), "%s", words[i]);
      }
      renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, leftMargin, lineY, line, !black);
      lineY += renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID);
    }

    if (!currentBook.author.empty()) {
      char authorBuf[64];
      snprintf(authorBuf, sizeof(authorBuf), "%s", currentBook.author.c_str());
      int authorY = bookY + bookHeight - 30;
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, leftMargin, authorY, authorBuf, !black);
    }
  }

  char titleBuf[128];
  if (!currentBook.title.empty()) {
    strncpy(titleBuf, currentBook.title.c_str(), sizeof(titleBuf));
  } else {
    std::string tmp = formatTitle(getBaseFilename(currentBook.path));
    strncpy(titleBuf, tmp.c_str(), sizeof(titleBuf));
  }
  titleBuf[sizeof(titleBuf) - 1] = '\0';

  int textX = coverItemX;
  int textY = coverAreaY + coverHeight + 25;
  int textWidth = containerWidth - 30;

  std::string trunc =
      renderer.truncatedText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, titleBuf, textWidth, EpdFontFamily::BOLD);

  int authorY = textY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID) + 2;
  char authorBuf[64];
  strncpy(authorBuf, currentBook.author.c_str(), sizeof(authorBuf));
  authorBuf[sizeof(authorBuf) - 1] = '\0';
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, authorY, trunc.c_str());

  float progress = hasStats ? stats.progressPercent : (currentBook.progress * 100.0f);
  if (progress >= 0) {
    int barX = textX;
    int barY = authorY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 5;
    int barW = textWidth;
    int barH = 5;

    renderer.fillRect(barX, barY, barW, barH, false);
    renderer.drawRect(barX, barY, barW, barH, true);

    if (progress > 0) {
      int fillW = static_cast<int>(barW * (progress / 100.0f));
      renderer.fillRect(barX, barY, fillW, barH);

      char pText[8];
      int percent;
      if (progress >= 99.5f) {
        percent = 100;
      } else {
        percent = static_cast<int>(progress + 0.5f);
      }
      snprintf(pText, sizeof(pText), "%d%%", percent);
      int pW = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pText);
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, barX + barW - pW - 2,
                        barY - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) - 5, pText);
    }
  }

  int statsX = detailsItemX + 15;
  int statsY = detailsItemY;
  int currentY = statsY + renderer.getLineHeight(VALUE_FONT) - 40;
  char buffer[32];

  uint32_t readingTime = hasStats ? stats.totalReadingTimeMs : 0;
  std::string timeStr = formatTime(readingTime);
  renderer.drawText(VALUE_FONT, statsX, currentY, timeStr.c_str(), true, EpdFontFamily::BOLD);

  if (hasStats && hasSecondStats && recentBooks.size() > 1) {
    int iconX = statsX + renderer.getTextWidth(VALUE_FONT, timeStr.c_str()) + 10;
    int iconY = currentY;
    renderer.drawIcon(stats.totalReadingTimeMs > secondStats.totalReadingTimeMs ? Up : Down, iconX, iconY + 10, 40, 40,
                      GfxRenderer::Rotate270CW);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, iconX + 45, iconY + 15,
                      formatTime(secondStats.totalReadingTimeMs).c_str(), true, EpdFontFamily::BOLD);
  }
  renderer.drawText(LABEL_FONT, statsX, currentY + 45, "Reading Time", true);
  currentY += 87;

  uint32_t pagesRead = hasStats ? stats.totalPagesRead : 0;
  snprintf(buffer, sizeof(buffer), "%u", pagesRead);
  renderer.drawText(VALUE_FONT, statsX, currentY, buffer, true, EpdFontFamily::BOLD);

  if (hasStats && hasSecondStats && recentBooks.size() > 1) {
    int iconX = statsX + renderer.getTextWidth(VALUE_FONT, buffer) + 10;
    int iconY = currentY;
    renderer.drawIcon(stats.totalPagesRead > secondStats.totalPagesRead ? Up : Down, iconX, iconY + 10, 40, 40,
                      GfxRenderer::Rotate270CW);
    char prevBuffer[32];
    snprintf(prevBuffer, sizeof(prevBuffer), "%u", secondStats.totalPagesRead);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, iconX + 45, iconY + 15, prevBuffer, true, EpdFontFamily::BOLD);
  }
  renderer.drawText(LABEL_FONT, statsX, currentY + 45, "Pages", true);
  currentY += 87;

  uint32_t chaptersRead = hasStats ? stats.totalChaptersRead : 0;
  snprintf(buffer, sizeof(buffer), "%u", chaptersRead);
  renderer.drawText(VALUE_FONT, statsX, currentY, buffer, true, EpdFontFamily::BOLD);

  if (hasStats && hasSecondStats && recentBooks.size() > 1) {
    int iconX = statsX + renderer.getTextWidth(VALUE_FONT, buffer) + 10;
    int iconY = currentY;
    renderer.drawIcon(stats.totalChaptersRead > secondStats.totalChaptersRead ? Up : Down, iconX, iconY + 10, 40, 40,
                      GfxRenderer::Rotate270CW);
    char prevBuffer[32];
    snprintf(prevBuffer, sizeof(prevBuffer), "%u", secondStats.totalChaptersRead);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, iconX + 45, iconY + 15, prevBuffer, true, EpdFontFamily::BOLD);
  }
  renderer.drawText(LABEL_FONT, statsX, currentY + 45, "Chapters", true);
  currentY += 87;

  uint32_t avgPageTime = hasStats ? stats.avgPageTimeMs : 0;
  if (avgPageTime > 0) {
    snprintf(buffer, sizeof(buffer), "%u s", avgPageTime / 1000);
  } else {
    snprintf(buffer, sizeof(buffer), "-");
  }
  renderer.drawText(VALUE_FONT, statsX, currentY, buffer, true, EpdFontFamily::BOLD);

  if (hasStats && hasSecondStats && recentBooks.size() > 1) {
    int iconX = statsX + renderer.getTextWidth(VALUE_FONT, buffer) + 10;
    int iconY = currentY;
    uint32_t prevAvgPageTime = secondStats.avgPageTimeMs;
    if (prevAvgPageTime > 0) {
      char prevBuffer[32];
      snprintf(prevBuffer, sizeof(prevBuffer), "%u s", prevAvgPageTime / 1000);
      renderer.drawIcon(avgPageTime > prevAvgPageTime ? Up : Down, iconX, iconY + 10, 40, 40, GfxRenderer::Rotate270CW);
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, iconX + 45, iconY + 15, prevBuffer, true,
                        EpdFontFamily::BOLD);
    } else if (secondStats.totalPagesRead > 0) {
      renderer.drawIcon(avgPageTime > 0 ? Up : Down, iconX, iconY + 10, 40, 40, GfxRenderer::Rotate270CW);
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, iconX + 45, iconY + 15, "-", true, EpdFontFamily::BOLD);
    }
  }
  renderer.drawText(LABEL_FONT, statsX, currentY + 45, "Average / Page", true);

  int bottomStartY = coverItemY + containerHeight + 20;
  int listEndY = screenH - 30;
  int availableListHeight = listEndY - bottomStartY;

  int itemsToShow = 2;
  int listIndex = 0;
  
  const int LIST_ITEM_HEIGHT = (renderer.getScreenHeight() - TAB_BAR_HEIGHT - 85) / 5 + 10;
  const int ITEM_SPACING = 10;
  
  int startIndex = 1 + scrollOffsetDefault;
  int endIndex = std::min(static_cast<int>(recentBooks.size()), startIndex + itemsToShow);
  
  for (int i = startIndex; i < endIndex; i++) {
    if (i < static_cast<int>(recentBooks.size())) {
      int itemIndex = i - startIndex;
      int itemY = bottomStartY + 20 + itemIndex * (LIST_ITEM_HEIGHT + ITEM_SPACING);
      if (itemIndex > 0) {
        int itemBorderY = itemY - 10;
        for (int x = 0; x < screenW; x += 2) {
          if (x >= 0 && x < renderer.getScreenWidth()) {
            renderer.drawPixel(x, itemBorderY, true);
          }
        }
      }
      
      bool isSelected = (selectorIndex == i);
      renderListItem(itemIndex, bottomStartY + 20, recentBooks[i], isSelected);
      listIndex++;
    }
  }

  int totalScrollableItems = static_cast<int>(recentBooks.size()) - 1;
  int visibleItemsCount = itemsToShow;
  
  if (totalScrollableItems > visibleItemsCount) {
    int scrollbarWidth = 4;
    int scrollbarHeight = availableListHeight - 50;
    int scrollbarX = screenW - scrollbarWidth - 10;
    int scrollbarY = bottomStartY + 30;
    int maxScrollOffset = std::max(0, totalScrollableItems - visibleItemsCount);
  
    renderer.fillRect(scrollbarX, scrollbarY, scrollbarWidth, scrollbarHeight, false);
    renderer.drawRect(scrollbarX, scrollbarY, scrollbarWidth, scrollbarHeight, true);
    
    float scrollRatio = static_cast<float>(scrollOffsetDefault) / maxScrollOffset;
    int thumbHeight = std::max(20, static_cast<int>(scrollbarHeight * visibleItemsCount / totalScrollableItems));
    int thumbY = scrollbarY + static_cast<int>(scrollRatio * (scrollbarHeight - thumbHeight));
    
    renderer.fillRect(scrollbarX, thumbY, scrollbarWidth, thumbHeight);
  }
}

/**
 * Main loop for handling user input and updating state.
 */
void RecentActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    updateRequired = true;
    return;
  }

  const int totalBooks = static_cast<int>(recentBooks.size());
  bool isDefaultView = (currentViewMode == ViewMode::Default);

  bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
  bool downPressed = mappedInput.wasPressed(MappedInputManager::Button::Down);
  bool leftPressed = mappedInput.wasPressed(MappedInputManager::Button::Left);
  bool rightPressed = mappedInput.wasPressed(MappedInputManager::Button::Right);
  bool confirmPressed = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);

  if (backPressed) {
    if (mappedInput.getHeldTime() >= GO_HOME_MS) return;
    onGoToRecent();
    return;
  }

  if (leftPressed) {
    tabSelectorIndex = 4;
    navigateToSelectedMenu();
    return;
  }

  if (rightPressed) {
    tabSelectorIndex = 1;
    navigateToSelectedMenu();
    return;
  }

  if (tabSelectorIndex != 0) {
    return;
  }

  if (totalBooks == 0) return;

  if (!isDefaultView) {
    ViewMode newViewMode = (SETTINGS.recentLibraryMode == SystemSetting::RECENT_GRID) ? ViewMode::Grid : ViewMode::List;
    if (newViewMode != currentViewMode) {
      currentViewMode = newViewMode;
      scrollOffset = 0;
      selectorIndex = 0;
      updateRequired = true;
      return;
    }
  }

  bool selectorChanged = false;

  if (isDefaultView) {
    int itemsToShow = 2;
    int maxScrollOffset = std::max(0, totalBooks - 1 - itemsToShow);
    
    if (scrollOffsetDefault < 0) scrollOffsetDefault = 0;
    if (scrollOffsetDefault > maxScrollOffset) scrollOffsetDefault = maxScrollOffset;
    
    if (downPressed) {
      if (selectorIndex < totalBooks - 1) {
        selectorIndex++;
        selectorChanged = true;
        if (selectorIndex > scrollOffsetDefault + itemsToShow) {
          scrollOffsetDefault = std::min(maxScrollOffset, scrollOffsetDefault + 1);
        }
        
        if (selectorIndex > 0) {
          statsSectionSelected = false;
        }
      }
    }

    if (upPressed) {
      if (selectorIndex > 0) {
        selectorIndex--;
        selectorChanged = true;
        if (selectorIndex < scrollOffsetDefault + 1) {
          scrollOffsetDefault = std::max(0, scrollOffsetDefault - 1);
        }
        
        if (selectorIndex == 0) {
          statsSectionSelected = true;
        }
      }
    }

    if (confirmPressed && selectorIndex >= 0 && selectorIndex < totalBooks) {
      bookSelected = true;
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }

    if (selectorChanged) {
      updateRequired = true;
    }
  } else {
    if (downPressed && selectorIndex < totalBooks - 1) {
      selectorIndex++;
      selectorChanged = true;
    } else if (upPressed && selectorIndex > 0) {
      selectorIndex--;
      selectorChanged = true;
    }

    if (selectorChanged) {
      if (currentViewMode == ViewMode::List) {
        int visibleItems = LIST_VISIBLE_ITEMS;
        if (selectorIndex < scrollOffset) {
          scrollOffset = selectorIndex;
        } else if (selectorIndex >= scrollOffset + visibleItems) {
          scrollOffset = selectorIndex - visibleItems + 1;
        }
        int maxOffset = std::max(0, totalBooks - visibleItems);
        scrollOffset = std::max(0, std::min(scrollOffset, maxOffset));
      } else {
        int currentRow = selectorIndex / GRID_COLS;
        int visibleRows = getVisibleRows();
        if (currentRow < scrollOffset) {
          scrollOffset = currentRow;
        } else if (currentRow >= scrollOffset + visibleRows) {
          scrollOffset = currentRow - visibleRows + 1;
        }
        int totalRows = (totalBooks + GRID_COLS - 1) / GRID_COLS;
        int maxOffset = std::max(0, totalRows - visibleRows);
        scrollOffset = std::max(0, std::min(scrollOffset, maxOffset));
      }
      updateRequired = true;
    }

    if (confirmPressed && selectorIndex >= 0 && selectorIndex < totalBooks) {
      bookSelected = true;
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
  }
}