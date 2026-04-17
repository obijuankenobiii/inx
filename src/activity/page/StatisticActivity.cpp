#include "StatisticActivity.h"

#include <Bitmap.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"

namespace {

/**
 * Thread-safe mutex operations wrapper
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
constexpr int GRID_SPACING = 15;
constexpr int GRID_COLS = 2;
constexpr int VALUE_FONT = ATKINSON_HYPERLEGIBLE_18_FONT_ID;
constexpr int LABEL_FONT = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
constexpr int PROGRESS_BAR_HEIGHT = 10;

}  // namespace

/**
 * Static task trampoline that forwards to the displayTaskLoop member function.
 */
void StatisticActivity::taskTrampoline(void* param) { static_cast<StatisticActivity*>(param)->displayTaskLoop(); }

/**
 * Background task loop that periodically checks if a display update is required.
 * Renders the display when updateRequired is true.
 */
void StatisticActivity::displayTaskLoop() {
  while (true) {
    {
      MutexGuard guard(renderingMutex);
      if (guard.isAcquired() && updateRequired) {
        updateRequired = false;
        render();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

/**
 * Loads reading statistics for all books and sorts them by most recently read.
 */
void StatisticActivity::loadStats() {
  allBooksStats = getAllBooksStats();

  std::sort(allBooksStats.begin(), allBooksStats.end(),
            [](const BookReadingStats& a, const BookReadingStats& b) { return a.lastReadTimeMs > b.lastReadTimeMs; });
}

/**
 * Formats a time duration in milliseconds to a human-readable string.
 * Output formats: "Xd Xh" for days, "Xh Xm" for hours, "Xm" for minutes.
 */
std::string StatisticActivity::formatTime(uint32_t milliseconds) const {
  char buffer[32];
  uint32_t seconds = milliseconds / 1000;
  uint32_t hours = seconds / 3600;
  uint32_t minutes = (seconds % 3600) / 60;
  uint32_t days = hours / 24;

  if (days > 0) {
    snprintf(buffer, sizeof(buffer), "%u %s %u %s", days, "d", hours % 24, "h");
  } else if (hours > 0) {
    snprintf(buffer, sizeof(buffer), "%u %s %u %s", hours, "h", minutes, "m");
  } else {
    snprintf(buffer, sizeof(buffer), "%u %s", minutes, "m");
  }
  return std::string(buffer);
}

/**
 * Renders a book cover image or a placeholder if the cover image cannot be loaded.
 * The cover is centered within the provided dimensions.
 */
void StatisticActivity::renderCover(const std::string& bookPath, int x, int y, int width, int height,
                                    const std::string& title, const std::string& author) const {
  std::string coverPath = bookPath + "/thumb.bmp";
  bool coverDrawn = false;

  FsFile file;
  if (SdMan.openFileForRead("COVER", coverPath.c_str(), file)) {
    Bitmap bitmap(file, bitmapDitherModeFromSetting(SETTINGS.displayImageDither));
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      int bw = bitmap.getWidth() > 225 ? 240 : bitmap.getWidth();
      int bh = bitmap.getHeight() > 340 ? 340 : bitmap.getHeight();

      int drawX = x + (width - bw) / 2;
      int drawY = y + (height - bh) / 2;

      int scaledW = bw * 90 / 100;
      int scaledH = bh * 90 / 100;

      BitmapGrayStyleScope displayGrayStyle(
          renderer, SETTINGS.displayImagePresentation == SystemSetting::IMAGE_PRESENTATION_FULL_GRAY
                        ? GfxRenderer::BitmapGrayRenderStyle::FullGray
                        : GfxRenderer::BitmapGrayRenderStyle::Balanced);
      renderer.drawBitmap(bitmap, drawX + 10, drawY + 5, scaledW, scaledH);
      coverDrawn = true;
    }
    file.close();
  }

  if (coverDrawn) {
    return;
  }

  renderer.drawRect(x, y, width, height);

  if (!title.empty()) {
    int lineY = y + 20;
    int maxWidth = width - 40;
    int lineHeight = renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID);

    std::string remaining = title;
    int lineCount = 0;

    while (!remaining.empty() && lineCount < 3) {
      std::string line;
      int lineWidth = 0;

      while (!remaining.empty()) {
        size_t spacePos = remaining.find(' ');
        std::string word = (spacePos != std::string::npos) ? remaining.substr(0, spacePos) : remaining;

        int wordWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, word.c_str(), EpdFontFamily::BOLD);

        if (lineWidth + wordWidth <= maxWidth) {
          if (!line.empty()) line += " ";
          line += word;
          lineWidth += wordWidth;

          if (spacePos != std::string::npos) {
            remaining = remaining.substr(spacePos + 1);
          } else {
            remaining.clear();
          }
        } else {
          break;
        }
      }

      if (line.empty()) {
        break;
      }

      int textWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
      int textX = x + (width - textWidth) / 2;
      renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, textX, lineY, line.c_str(), true, EpdFontFamily::BOLD);
      lineY += lineHeight;
      lineCount++;
    }
  }

  if (!author.empty()) {
    std::string authorText = "- " + author;
    int authorWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, authorText.c_str());
    int authorX = x + (width - authorWidth) / 2;
    int authorY = y + height - 30;
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, authorX, authorY, authorText.c_str());
  }
}

/**
 * Lifecycle hook called when entering the activity.
 * Initializes rendering resources and starts the display task.
 */
void StatisticActivity::onEnter() {
  Activity::onEnter();

  loadStats();

  renderingMutex = xSemaphoreCreateMutex();
  if (!renderingMutex) return;

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  render();

  if (displayTaskHandle == nullptr) {
    xTaskCreate(&StatisticActivity::taskTrampoline, "StatisticTask", 4096, this, 1, &displayTaskHandle);
  }
}

/**
 * Lifecycle hook called when exiting the activity.
 * Cleans up rendering resources and stops the display task.
 */
void StatisticActivity::onExit() {
  Activity::onExit();

  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }

  allBooksStats.clear();
  allBooksStats.shrink_to_fit();
}

/**
 * Renders the complete statistics view with book covers, progress bars,
 * and reading statistics in a 2x2 grid layout.
 */
void StatisticActivity::render() const {
  renderer.clearScreen();
  renderTabBar(renderer);

  if (allBooksStats.empty()) {
    int screenHeight = renderer.getScreenHeight();
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, screenHeight / 2, "No available data");
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, screenHeight / 2 + 30, "Reading statistics shown here");
    renderer.displayBuffer();
    return;
  }

  int screenWidth = renderer.getScreenWidth();
  int screenHeight = renderer.getScreenHeight();

  int availableWidth = screenWidth - (GRID_COLS + 1) * GRID_SPACING;
  int containerWidth = availableWidth / GRID_COLS;

  int contentStartY = TAB_BAR_HEIGHT + 5;
  int availableHeight = screenHeight - contentStartY - GRID_SPACING;
  int containerHeight = (availableHeight / 2) - GRID_SPACING;

  int startIndex = bookSelectorIndex * 2;

  for (int row = 0; row < 2; row++) {
    int bookIndex = startIndex + row;
    if (bookIndex >= static_cast<int>(allBooksStats.size())) break;

    int rowY = contentStartY + row * (containerHeight + GRID_SPACING);
    int coverX = GRID_SPACING;
    int coverY = rowY;
    int coverWidth = containerWidth;
    int coverHeight = containerHeight;

    int detailsX = coverX + containerWidth + GRID_SPACING;
    int detailsY = rowY - 15;

    renderer.drawRect(coverX, coverY, coverWidth, coverHeight, false);

    int coverImageWidth = coverWidth - 20;
    int coverImageHeight = coverHeight - 40;
    int coverImageX = coverX + (coverWidth - coverImageWidth) / 2;
    int coverImageY = coverY + 25;

    renderCover(allBooksStats[bookIndex].path, coverImageX, coverImageY, coverImageWidth, coverImageHeight,
                allBooksStats[bookIndex].title, allBooksStats[bookIndex].author);

    float progress = allBooksStats[bookIndex].progressPercent;
    if (progress >= 0) {
      int barX = coverImageX;
      int barY = coverImageY + coverImageHeight + 10;
      int barW = coverImageWidth;
      int barH = PROGRESS_BAR_HEIGHT;

      renderer.fillRect(barX, barY, barW, barH, false);
      renderer.drawRect(barX, barY, barW, barH, true);

      if (progress > 0) {
        int fillW = static_cast<int>(barW * (progress / 100.0f));
        renderer.fillRect(barX, barY, fillW, barH);
      }
    }

    int textX = detailsX + 15;
    int textY = detailsY + 20;
    int lineHeight = renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID);
    int labelSpacing = 5;
    char buffer[32];

    float progressPercent = allBooksStats[bookIndex].progressPercent;
    if (progressPercent >= 0) {
      snprintf(buffer, sizeof(buffer), "%.0f%%", progressPercent);
      renderer.drawText(VALUE_FONT, textX, textY, buffer, true, EpdFontFamily::BOLD);
      renderer.drawText(LABEL_FONT, textX, textY + lineHeight + labelSpacing, "Progress", true);
      textY += 70;
    }

    std::string timeStr = formatTime(allBooksStats[bookIndex].totalReadingTimeMs);
    renderer.drawText(VALUE_FONT, textX, textY, timeStr.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawText(LABEL_FONT, textX, textY + lineHeight + labelSpacing, "Reading Time", true);
    textY += 70;

    snprintf(buffer, sizeof(buffer), "%u", allBooksStats[bookIndex].totalPagesRead);
    renderer.drawText(VALUE_FONT, textX, textY, buffer, true, EpdFontFamily::BOLD);
    renderer.drawText(LABEL_FONT, textX, textY + lineHeight + labelSpacing, "Pages", true);
    textY += 70;

    snprintf(buffer, sizeof(buffer), "%u", allBooksStats[bookIndex].totalChaptersRead);
    renderer.drawText(VALUE_FONT, textX, textY, buffer, true, EpdFontFamily::BOLD);
    renderer.drawText(LABEL_FONT, textX, textY + lineHeight + labelSpacing, "Chapters", true);
    textY += 70;

    if (allBooksStats[bookIndex].avgPageTimeMs > 0) {
      snprintf(buffer, sizeof(buffer), "%us", allBooksStats[bookIndex].avgPageTimeMs / 1000);
    } else {
      snprintf(buffer, sizeof(buffer), "-");
    }
    renderer.drawText(VALUE_FONT, textX, textY, buffer, true, EpdFontFamily::BOLD);
    renderer.drawText(LABEL_FONT, textX, textY + lineHeight + labelSpacing, "Average / Page", true);
  }

  int totalItems = allBooksStats.size();
  int itemsPerPage = 2;
  int totalPages = (totalItems + itemsPerPage - 1) / itemsPerPage;

  if (totalPages > 1) {
    int scrollbarWidth = 4;
    int scrollbarHeight = 60;
    int scrollbarX = screenWidth - scrollbarWidth - 10;
    int scrollbarY = (screenHeight - scrollbarHeight) / 2;

    renderer.fillRect(scrollbarX, scrollbarY, scrollbarWidth, scrollbarHeight, false);
    renderer.drawRect(scrollbarX, scrollbarY, scrollbarWidth, scrollbarHeight, true);

    float scrollRatio = static_cast<float>(bookSelectorIndex) / (totalPages - 1);
    int thumbHeight = std::max(20, static_cast<int>(scrollbarHeight / totalPages));
    int thumbY = scrollbarY + static_cast<int>(scrollRatio * (scrollbarHeight - thumbHeight));

    renderer.fillRect(scrollbarX, thumbY, scrollbarWidth, thumbHeight);
  }

  renderer.displayBuffer();
}

/**
 * Main loop for handling user input and updating the display state.
 * Processes button presses for tab navigation and page scrolling.
 */
void StatisticActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    updateRequired = true;
    return;
  }
  
  if (Activity::mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (Activity::mappedInput.getHeldTime() >= GO_HOME_MS) return;
    onGoToRecent();
    return;
  }

  const bool leftPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool rightPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Right);
  const bool upPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool downPressed = Activity::mappedInput.wasPressed(MappedInputManager::Button::Down);

  if (leftPressed) {
    tabSelectorIndex = 3;
    navigateToSelectedMenu();
    return;
  }

  if (rightPressed) {
    tabSelectorIndex = 0;
    navigateToSelectedMenu();
    return;
  }

  if (tabSelectorIndex != 4) {
    return;
  }

  if (allBooksStats.empty()) return;

  int booksPerPage = 2;
  int totalPages = (allBooksStats.size() + booksPerPage - 1) / booksPerPage;

  if (upPressed) {
    if (bookSelectorIndex > 0) {
      bookSelectorIndex--;
      updateRequired = true;
    }
    return;
  }

  if (downPressed) {
    if (bookSelectorIndex < totalPages - 1) {
      bookSelectorIndex++;
      updateRequired = true;
    }
    return;
  }
}