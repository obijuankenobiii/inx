/**
 * @file XtcReaderActivity.cpp
 * @brief Definitions for XtcReaderActivity.
 */

/**
 * XtcReaderActivity.cpp
 *
 * XTC ebook reader activity implementation
 * Displays pre-rendered XTC pages on e-ink display
 */

#include "XtcReaderActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "state/RecentBooks.h"
#include "state/Session.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr unsigned long skipPageMs = 700;
constexpr unsigned long goHomeMs = 1000;
constexpr unsigned long STATS_SAVE_INTERVAL_MS = 30000;
constexpr int XTC_MENU_ITEM_HEIGHT = 60;

uint8_t xtcQualityGray2Code(const uint8_t level, const bool deviceIsX3) {
  const uint8_t l = level & 3u;
  if (deviceIsX3) {
    return l;
  }
  if (l == 1u) return 2u;
  if (l == 2u) return 1u;
  return l;
}

int chapterIndexForPage(const Xtc& book, uint32_t page) {
  if (!book.hasChapters()) {
    return 0;
  }
  const auto& ch = book.getChapters();
  int best = -1;
  for (size_t i = 0; i < ch.size(); i++) {
    if (page >= static_cast<uint32_t>(ch[i].startPage)) {
      best = static_cast<int>(i);
    }
  }
  return best < 0 ? 0 : best;
}

std::string formatReadingTime(const uint32_t timeMs) {
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

bool isLandscapeReader(const GfxRenderer& gfx) {
  const auto o = gfx.getOrientation();
  return o == GfxRenderer::LandscapeClockwise || o == GfxRenderer::LandscapeCounterClockwise;
}

bool xtcDrawerPrev(const MappedInputManager& input, const GfxRenderer& renderer) {
  return isLandscapeReader(renderer) ? input.wasPressed(MappedInputManager::Button::Right)
                                     : input.wasPressed(MappedInputManager::Button::Up);
}

bool xtcDrawerNext(const MappedInputManager& input, const GfxRenderer& renderer) {
  return isLandscapeReader(renderer) ? input.wasPressed(MappedInputManager::Button::Left)
                                     : input.wasPressed(MappedInputManager::Button::Down);
}

bool xtcValueDecrease(const MappedInputManager& input, const GfxRenderer& renderer) {
  return isLandscapeReader(renderer) ? input.wasPressed(MappedInputManager::Button::Down)
                                     : input.wasPressed(MappedInputManager::Button::Left);
}

bool xtcValueIncrease(const MappedInputManager& input, const GfxRenderer& renderer) {
  return isLandscapeReader(renderer) ? input.wasPressed(MappedInputManager::Button::Up)
                                     : input.wasPressed(MappedInputManager::Button::Right);
}

const char* qualityLabel(const uint8_t quality) {
  switch (quality) {
    case SystemSetting::READER_IMAGE_MEDIUM:
      return "Medium";
    case SystemSetting::READER_IMAGE_HIGH:
      return "High";
    default:
      return "Low";
  }
}

const char* autoTurnLabel() {
  static char buf[12];
  if (SETTINGS.xtcPageAutoTurnSeconds == 0) {
    return "Off";
  }
  snprintf(buf, sizeof(buf), "%u sec", SETTINGS.xtcPageAutoTurnSeconds);
  return buf;
}

const char* refreshLabel() {
  static char buf[12];
  snprintf(buf, sizeof(buf), "%u page%s", SETTINGS.xtcRefreshFrequency, SETTINGS.xtcRefreshFrequency == 1 ? "" : "s");
  return buf;
}

const char* powerLabel() {
  return SETTINGS.xtcShortPwrBtn == SystemSetting::XTC_POWER_PAGE_REFRESH ? "Page Refresh" : "Next";
}

void changeAutoTurn(const int delta) {
  int value = static_cast<int>(SETTINGS.xtcPageAutoTurnSeconds) + delta * 10;
  if (value < 0) value = 60;
  if (value > 60) value = 0;
  SETTINGS.xtcPageAutoTurnSeconds = static_cast<uint8_t>(value);
}

void changeRefreshFrequency(const int delta) {
  static constexpr uint8_t values[] = {1, 5, 10, 15, 30};
  constexpr int count = static_cast<int>(sizeof(values) / sizeof(values[0]));
  int idx = 3;
  for (int i = 0; i < count; ++i) {
    if (values[i] == SETTINGS.xtcRefreshFrequency) {
      idx = i;
      break;
    }
  }
  idx = (idx + (delta >= 0 ? 1 : count - 1)) % count;
  SETTINGS.xtcRefreshFrequency = values[idx];
}

void changeQuality(const int delta) {
  const int count = SystemSetting::READER_IMAGE_QUALITY_COUNT;
  int value = static_cast<int>(SETTINGS.xtcImageQuality) + delta;
  value = (value % count + count) % count;
  SETTINGS.xtcImageQuality = static_cast<uint8_t>(value);
}

void changePowerButton() {
  SETTINGS.xtcShortPwrBtn = SETTINGS.xtcShortPwrBtn == SystemSetting::XTC_POWER_NEXT
                                ? SystemSetting::XTC_POWER_PAGE_REFRESH
                                : SystemSetting::XTC_POWER_NEXT;
}
}  // namespace

void XtcReaderActivity::taskTrampoline(void* param) {
  auto* self = static_cast<XtcReaderActivity*>(param);
  self->displayTaskLoop();
}

void XtcReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!xtc) {
    return;
  }

  renderingMutex = xSemaphoreCreateMutex();

  xtc->setupCacheDir();
  loadProgress();
  ensureThumbnailExists();
  initStats();

  APP_STATE.lastRead = xtc->getPath();
  APP_STATE.saveToFile();

  const uint32_t n = xtc->getPageCount();
  float progressFrac = 0.f;
  if (n > 0) {
    progressFrac = (static_cast<float>(std::min(currentPage, n - 1)) + 1.f) / static_cast<float>(n);
  }
  RECENT_BOOKS.addBook(xtc->getPath(), xtc->getCachePath(), xtc->getTitle(), xtc->getAuthor(), progressFrac);

  updateRequired = true;

  xTaskCreate(&XtcReaderActivity::taskTrampoline, "XtcReaderActivityTask", 4096, this, 1, &displayTaskHandle);
}

void XtcReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vTaskDelay(pdMS_TO_TICKS(10));

  if (pageStartTime > 0) {
    endPageTimer();
  }
  if (xtc) {
    saveBookStatsToFile();
    saveProgress();
    const uint32_t n = xtc->getPageCount();
    uint32_t progPage = currentPage;
    if (n > 0 && progPage >= n) {
      progPage = n - 1;
    }
    const float progressFrac = (n > 0) ? (static_cast<float>(progPage) + 1.f) / static_cast<float>(n) : 0.f;
    RECENT_BOOKS.addBook(xtc->getPath(), xtc->getCachePath(), xtc->getTitle(), xtc->getAuthor(), progressFrac);
    APP_STATE.lastRead = xtc->getPath();
    APP_STATE.saveToFile();
  }

  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  xtc.reset();
}

void XtcReaderActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (suppressBackUntilReleased) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back)) {
      suppressBackUntilReleased = false;
    }
    return;
  }

  if (menuDrawerVisible) {
    handleMenuDrawerInput();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openMenuDrawer();
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= goHomeMs) {
    onGoToRecent();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < goHomeMs) {
    onGoBack();
    return;
  }

  const bool usePressForPageTurn = SETTINGS.longPressChapterSkip == SystemSetting::LONG_PRESS_OFF;
  const MappedInputManager::MotionGesture motionGesture =
      mappedInput.readMotionGesture(static_cast<uint8_t>(renderer.getOrientation()), SETTINGS.shakePageTurn,
                                    SETTINGS.shakePageTurnSensitivity);
  const bool prevTriggered =
      motionGesture == MappedInputManager::MotionGesture::Previous ||
      (usePressForPageTurn ? (mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                              mappedInput.wasPressed(MappedInputManager::Button::Left))
                           : (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Left)));
  if (SETTINGS.xtcShortPwrBtn == SystemSetting::XTC_POWER_PAGE_REFRESH &&
      mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    renderer.displayBuffer(HalDisplay::MANUAL_REFRESH);
    return;
  }

  const bool powerPageTurn = SETTINGS.xtcShortPwrBtn == SystemSetting::XTC_POWER_NEXT &&
                             mappedInput.wasReleased(MappedInputManager::Button::Power);
  const bool nextTriggered =
      motionGesture == MappedInputManager::MotionGesture::Next ||
      (usePressForPageTurn
           ? (mappedInput.wasPressed(MappedInputManager::Button::PageForward) || powerPageTurn ||
              mappedInput.wasPressed(MappedInputManager::Button::Right))
           : (mappedInput.wasReleased(MappedInputManager::Button::PageForward) || powerPageTurn ||
              mappedInput.wasReleased(MappedInputManager::Button::Right)));

  if (!prevTriggered && !nextTriggered) {
    if (SETTINGS.xtcPageAutoTurnSeconds > 0 && xtc && xtc->getPageCount() > 0 && currentPage < xtc->getPageCount() &&
        pageStartTime > 0 &&
        millis() - pageStartTime >= static_cast<uint32_t>(SETTINGS.xtcPageAutoTurnSeconds) * 1000UL) {
      turnPage(true);
    }
    return;
  }

  if (xtc->getPageCount() == 0) {
    return;
  }

  const uint32_t pageCount = xtc->getPageCount();

  if (currentPage >= pageCount) {
    turnPage(false);
    return;
  }

  const bool skipPages =
      SETTINGS.longPressChapterSkip != SystemSetting::LONG_PRESS_OFF && mappedInput.getHeldTime() > skipPageMs;
  const int skipAmount =
      !skipPages ? 1 : (SETTINGS.longPressChapterSkip == SystemSetting::LONG_PRESS_PAGE_SKIP_5 ? 5 : 10);

  if (prevTriggered) {
    turnPage(false, skipAmount);
  } else if (nextTriggered) {
    turnPage(true, skipAmount);
  }
}

int XtcReaderActivity::chapterIndexForCurrentPage() const {
  if (!xtc || xtc->getPageCount() == 0) {
    return 0;
  }
  const uint32_t page = std::min<uint32_t>(currentPage, xtc->getPageCount() - 1);
  return chapterIndexForPage(*xtc, page);
}

void XtcReaderActivity::turnPage(const bool forward, const int skipAmount) {
  if (!xtc || xtc->getPageCount() == 0) {
    return;
  }

  const uint32_t pageCount = xtc->getPageCount();
  if (currentPage >= pageCount) {
    endPageTimer();
    currentPage = pageCount - 1;
    startPageTimer();
    updateRequired = true;
    return;
  }

  endPageTimer();
  if (forward) {
    const uint32_t oldPage = currentPage;
    const int oldChapter = chapterIndexForPage(*xtc, oldPage);
    currentPage += static_cast<uint32_t>(std::max(1, skipAmount));
    if (currentPage >= pageCount) {
      currentPage = pageCount;
    }
    const uint32_t refPage = std::min(currentPage, pageCount - 1);
    const int newChapter = chapterIndexForPage(*xtc, refPage);
    if (newChapter > oldChapter) {
      bookStats.totalChaptersRead += static_cast<uint32_t>(newChapter - oldChapter);
    }
  } else if (currentPage >= static_cast<uint32_t>(std::max(1, skipAmount))) {
    currentPage -= static_cast<uint32_t>(std::max(1, skipAmount));
  } else {
    currentPage = 0;
  }
  startPageTimer();
  updateRequired = true;
}

void XtcReaderActivity::openMenuDrawer() {
  menuDrawerVisible = true;
  menuDrawerShowingChapters = false;
  menuSelectedIndex = 0;
  menuScrollOffset = 0;
  chapterSelectedIndex = chapterIndexForCurrentPage();
  chapterScrollOffset = std::max(0, chapterSelectedIndex - 2);
  renderMenuDrawer();
}

void XtcReaderActivity::closeMenuDrawer(const bool repaintPage) {
  menuDrawerVisible = false;
  menuDrawerShowingChapters = false;
  if (repaintPage) {
    updateRequired = true;
  }
}

void XtcReaderActivity::handleMenuDrawerInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (menuDrawerShowingChapters) {
      menuDrawerShowingChapters = false;
      renderMenuDrawer();
    } else {
      suppressBackUntilReleased = true;
      closeMenuDrawer(true);
    }
    return;
  }

  if (!menuDrawerShowingChapters) {
    constexpr int itemCount = 6;
    if (xtcDrawerPrev(mappedInput, renderer)) {
      menuSelectedIndex = (menuSelectedIndex - 1 + itemCount) % itemCount;
      if (menuSelectedIndex < menuScrollOffset) menuScrollOffset = menuSelectedIndex;
      renderMenuDrawer();
      return;
    }
    if (xtcDrawerNext(mappedInput, renderer)) {
      menuSelectedIndex = (menuSelectedIndex + 1) % itemCount;
      renderMenuDrawer();
      return;
    }

    int settingDelta = 0;
    if (xtcValueDecrease(mappedInput, renderer)) settingDelta = -1;
    if (xtcValueIncrease(mappedInput, renderer)) settingDelta = 1;
    if (settingDelta != 0 && menuSelectedIndex >= 1 && menuSelectedIndex <= 4) {
      if (menuSelectedIndex == 1) changeAutoTurn(settingDelta);
      if (menuSelectedIndex == 2) changeRefreshFrequency(settingDelta);
      if (menuSelectedIndex == 3) changeQuality(settingDelta);
      if (menuSelectedIndex == 4) changePowerButton();
      SETTINGS.saveToFile();
      pagesUntilFullRefresh = SETTINGS.xtcRefreshFrequency;
      renderMenuDrawer();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (menuSelectedIndex == 0) {
        if (xtc && xtc->hasChapters() && !xtc->getChapters().empty()) {
          menuDrawerShowingChapters = true;
          chapterSelectedIndex = chapterIndexForCurrentPage();
          chapterScrollOffset = std::max(0, chapterSelectedIndex - 2);
          renderMenuDrawer();
        }
      } else if (menuSelectedIndex == 1) {
        changeAutoTurn(1);
        SETTINGS.saveToFile();
        renderMenuDrawer();
      } else if (menuSelectedIndex == 2) {
        changeRefreshFrequency(1);
        SETTINGS.saveToFile();
        pagesUntilFullRefresh = SETTINGS.xtcRefreshFrequency;
        renderMenuDrawer();
      } else if (menuSelectedIndex == 3) {
        changeQuality(1);
        SETTINGS.saveToFile();
        renderMenuDrawer();
      } else if (menuSelectedIndex == 4) {
        changePowerButton();
        SETTINGS.saveToFile();
        renderMenuDrawer();
      } else if (menuSelectedIndex == 5) {
        closeMenuDrawer(false);
        onGoToRecent();
      }
      return;
    }
    return;
  }

  if (!xtc || !xtc->hasChapters() || xtc->getChapters().empty()) {
    menuDrawerShowingChapters = false;
    renderMenuDrawer();
    return;
  }

  const int chapterCount = static_cast<int>(xtc->getChapters().size());
  if (xtcDrawerPrev(mappedInput, renderer)) {
    chapterSelectedIndex = (chapterSelectedIndex - 1 + chapterCount) % chapterCount;
    if (chapterSelectedIndex < chapterScrollOffset) chapterScrollOffset = chapterSelectedIndex;
    renderMenuDrawer();
    return;
  }
  if (xtcDrawerNext(mappedInput, renderer)) {
    chapterSelectedIndex = (chapterSelectedIndex + 1) % chapterCount;
    renderMenuDrawer();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    endPageTimer();
    currentPage = static_cast<uint32_t>(xtc->getChapters()[chapterSelectedIndex].startPage);
    startPageTimer();
    closeMenuDrawer(true);
  }
}

void XtcReaderActivity::renderMenuDrawer() {
  if (menuDrawerShowingChapters) {
    renderMenuChapters();
  } else {
    renderMenuMain();
  }
}

void XtcReaderActivity::renderMenuMain() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const bool landscape = isLandscapeReader(renderer);
  const int drawerW = landscape ? screenW / 2 : screenW;
  const int drawerH = landscape ? screenH : screenH * 60 / 100;
  const int drawerX = landscape ? screenW - drawerW : 0;
  const int drawerY = landscape ? 0 : screenH - drawerH;
  constexpr int headerH = 64;
  constexpr int bottomPad = 8;
  const int visibleRows = std::max(1, (drawerH - headerH - bottomPad) / XTC_MENU_ITEM_HEIGHT);
  constexpr int itemCount = 6;
  if (menuSelectedIndex >= menuScrollOffset + visibleRows) menuScrollOffset = menuSelectedIndex - visibleRows + 1;
  if (menuSelectedIndex < menuScrollOffset) menuScrollOffset = menuSelectedIndex;
  menuScrollOffset = std::max(0, std::min(menuScrollOffset, std::max(0, itemCount - visibleRows)));

  renderer.rectangle.fill(drawerX, drawerY, drawerW, drawerH, false);
  renderer.rectangle.render(drawerX, drawerY, drawerW, drawerH, true);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, drawerX + 18, drawerY + 18, "XTC Menu", true,
                       EpdFontFamily::BOLD);
  renderer.line.render(drawerX, drawerY + headerH - 1, drawerX + drawerW, drawerY + headerH - 1, true);

  const char* labels[itemCount] = {"Table of Contents", "Auto Page Turn", "Page Until Refresh",
                                   "Image Quality",     "Power Button",   "Go Home"};
  const char* values[itemCount] = {
      "", autoTurnLabel(), refreshLabel(), qualityLabel(SETTINGS.xtcImageQuality), powerLabel(), ""};

  for (int i = 0; i < visibleRows && menuScrollOffset + i < itemCount; ++i) {
    const int idx = menuScrollOffset + i;
    const int rowY = drawerY + headerH + i * XTC_MENU_ITEM_HEIGHT;
    const bool selected = idx == menuSelectedIndex;
    renderer.rectangle.fill(
        drawerX + 1, rowY, drawerW - 2, XTC_MENU_ITEM_HEIGHT,
        selected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
    const int textY = rowY + (XTC_MENU_ITEM_HEIGHT - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, drawerX + 18, textY, labels[idx], selected ? 0 : 1,
                         idx == 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (values[idx][0] != '\0') {
      const int valueW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, values[idx]);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, drawerX + drawerW - 18 - valueW, textY, values[idx],
                           selected ? 0 : 1);
    }
    renderer.line.render(drawerX, rowY + XTC_MENU_ITEM_HEIGHT - 1, drawerX + drawerW, rowY + XTC_MENU_ITEM_HEIGHT - 1,
                         true);
  }

  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "Back", "Select", "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void XtcReaderActivity::renderMenuChapters() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const bool landscape = isLandscapeReader(renderer);
  const int drawerW = landscape ? screenW / 2 : screenW;
  const int drawerH = landscape ? screenH : screenH * 60 / 100;
  const int drawerX = landscape ? screenW - drawerW : 0;
  const int drawerY = landscape ? 0 : screenH - drawerH;
  constexpr int headerH = 64;
  constexpr int bottomPad = 8;
  const int visibleRows = std::max(1, (drawerH - headerH - bottomPad) / XTC_MENU_ITEM_HEIGHT);
  const int chapterCount = xtc ? static_cast<int>(xtc->getChapters().size()) : 0;
  if (chapterCount <= 0) {
    menuDrawerShowingChapters = false;
    renderMenuMain();
    return;
  }
  if (chapterSelectedIndex >= chapterScrollOffset + visibleRows) {
    chapterScrollOffset = chapterSelectedIndex - visibleRows + 1;
  }
  if (chapterSelectedIndex < chapterScrollOffset) chapterScrollOffset = chapterSelectedIndex;
  chapterScrollOffset = std::max(0, std::min(chapterScrollOffset, std::max(0, chapterCount - visibleRows)));

  renderer.rectangle.fill(drawerX, drawerY, drawerW, drawerH, false);
  renderer.rectangle.render(drawerX, drawerY, drawerW, drawerH, true);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, drawerX + 18, drawerY + 18, "Chapters", true,
                       EpdFontFamily::BOLD);
  renderer.line.render(drawerX, drawerY + headerH - 1, drawerX + drawerW, drawerY + headerH - 1, true);

  const auto& chapters = xtc->getChapters();
  for (int i = 0; i < visibleRows && chapterScrollOffset + i < chapterCount; ++i) {
    const int idx = chapterScrollOffset + i;
    const int rowY = drawerY + headerH + i * XTC_MENU_ITEM_HEIGHT;
    const bool selected = idx == chapterSelectedIndex;
    renderer.rectangle.fill(
        drawerX + 1, rowY, drawerW - 2, XTC_MENU_ITEM_HEIGHT,
        selected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));
    const int textY = rowY + (XTC_MENU_ITEM_HEIGHT - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
    const std::string& title = chapters[idx].name;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, drawerX + 18, textY,
                         title.empty() ? "Chapter" : title.c_str(), selected ? 0 : 1);
    char pageLabel[16];
    snprintf(pageLabel, sizeof(pageLabel), "%d", chapters[idx].startPage + 1);
    const int labelW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, pageLabel);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, drawerX + drawerW - 18 - labelW, textY + 2, pageLabel,
                         selected ? 0 : 1);
    renderer.line.render(drawerX, rowY + XTC_MENU_ITEM_HEIGHT - 1, drawerX + drawerW, rowY + XTC_MENU_ITEM_HEIGHT - 1,
                         true);
  }

  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "Menu", "Open", "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void XtcReaderActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      renderScreen();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void XtcReaderActivity::renderScreen() {
  if (!xtc) {
    return;
  }

  if (currentPage >= xtc->getPageCount()) {
    const uint32_t pageCount = xtc->getPageCount();
    if (pageCount > 0) {
      bookStats.progressPercent = 100.f;
      bookStats.lastPageNumber = static_cast<uint16_t>(std::min<uint32_t>(pageCount - 1, UINT16_MAX));
      bookStats.lastSpineIndex = static_cast<uint16_t>(chapterIndexForPage(*xtc, pageCount - 1));
    }
    saveBookStatsToFile();
    renderEndOfBookStats();
    return;
  }

  renderPage();
  saveProgress();
}

void XtcReaderActivity::renderEndOfBookStats() {
  renderer.clearScreen(0xff);

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  constexpr int valueFont = ATKINSON_HYPERLEGIBLE_18_FONT_ID;
  constexpr int labelFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  const int statsX = (screenW - 250) / 2;
  const int statsY = (screenH - 300) / 2;
  int currentY = statsY;
  char buffer[32];

  renderer.text.render(ATKINSON_HYPERLEGIBLE_18_FONT_ID, statsX, statsY - 90, "End of book", true, EpdFontFamily::BOLD);

  const std::string timeStr = formatReadingTime(bookStats.totalReadingTimeMs);
  renderer.text.render(valueFont, statsX, currentY, timeStr.c_str(), true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Reading Time", true);
  currentY += 87;

  snprintf(buffer, sizeof(buffer), "%u", bookStats.totalPagesRead);
  renderer.text.render(valueFont, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Pages", true);
  currentY += 87;

  snprintf(buffer, sizeof(buffer), "%u", bookStats.totalChaptersRead);
  renderer.text.render(valueFont, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Chapters", true);
  currentY += 87;

  if (bookStats.avgPageTimeMs > 0) {
    snprintf(buffer, sizeof(buffer), "%us", bookStats.avgPageTimeMs / 1000);
  } else {
    snprintf(buffer, sizeof(buffer), "-");
  }
  renderer.text.render(valueFont, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Average / Page", true);

  currentY += 87;
  snprintf(buffer, sizeof(buffer), "%u", bookStats.sessionCount);
  renderer.text.render(valueFont, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Reading Sessions", true);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void XtcReaderActivity::renderPage() {
  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();

  size_t pageBufferSize;
  if (bitDepth == 2) {
    pageBufferSize = ((static_cast<size_t>(pageWidth) * pageHeight + 7) / 8) * 2;
  } else {
    pageBufferSize = ((pageWidth + 7) / 8) * pageHeight;
  }

  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(pageBufferSize));
  if (!pageBuffer) {
    Serial.printf("[%lu] [XTR] Failed to allocate page buffer (%lu bytes)\n", millis(), pageBufferSize);
    renderer.clearScreen();
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 300, "Memory error", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  size_t bytesRead = xtc->loadPage(currentPage, pageBuffer, pageBufferSize);
  if (bytesRead == 0) {
    Serial.printf("[%lu] [XTR] Failed to load page %lu\n", millis(), currentPage);
    free(pageBuffer);
    renderer.clearScreen();
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 300, "Page load error", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  renderer.clearScreen();

  const uint16_t maxSrcY = pageHeight;

  if (bitDepth == 2) {
    const size_t planeSize = (static_cast<size_t>(pageWidth) * pageHeight + 7) / 8;
    const uint8_t* plane1 = pageBuffer;
    const uint8_t* plane2 = pageBuffer + planeSize;
    const size_t colBytes = (pageHeight + 7) / 8;

    auto getPixelValue = [&](uint16_t x, uint16_t y) -> uint8_t {
      const size_t colIndex = pageWidth - 1 - x;
      const size_t byteInCol = y / 8;
      const size_t bitInByte = 7 - (y % 8);
      const size_t byteOffset = colIndex * colBytes + byteInCol;
      const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
      const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
      return (bit1 << 1) | bit2;
    };

    auto renderBwPreview = [&] {
      renderer.clearScreen();
      for (uint16_t y = 0; y < pageHeight; y++) {
        for (uint16_t x = 0; x < pageWidth; x++) {
          if (getPixelValue(x, y) >= 1) {
            renderer.drawPixel(x, y, true);
          }
        }
      }
    };

    auto updateRefreshCadence = [&] {
      if (pagesUntilFullRefresh <= 1) {
        pagesUntilFullRefresh = SETTINGS.xtcRefreshFrequency;
      } else {
        pagesUntilFullRefresh--;
      }
    };

    const uint8_t imageQuality = SETTINGS.xtcImageQuality < SystemSetting::READER_IMAGE_QUALITY_COUNT
                                     ? SETTINGS.xtcImageQuality
                                     : SystemSetting::READER_IMAGE_LOW;

    if (imageQuality == SystemSetting::READER_IMAGE_LOW) {
      renderBwPreview();
      if (pagesUntilFullRefresh <= 1) {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        pagesUntilFullRefresh = SETTINGS.xtcRefreshFrequency;
      } else {
        renderer.displayBuffer();
        pagesUntilFullRefresh--;
      }

      free(pageBuffer);
      Serial.printf("[%lu] [XTR] Rendered page %lu/%lu (2-bit low/BW)\n", millis(), currentPage + 1,
                    xtc->getPageCount());
      return;
    }

    if (imageQuality == SystemSetting::READER_IMAGE_HIGH) {
      renderer.renderGrayscalePasses(/*quality=*/true, /*preserveText=*/false,
                                     [&] {
                                       renderer.clearScreen(0xFF);
                                       const bool x3 = renderer.deviceIsX3();
                                       const GfxRenderer::RenderMode renderMode = renderer.getRenderMode();
                                       for (uint16_t y = 0; y < pageHeight; y++) {
                                         for (uint16_t x = 0; x < pageWidth; x++) {
                                           const uint8_t code = xtcQualityGray2Code(getPixelValue(x, y), x3);
                                           if ((renderMode == GfxRenderer::GRAY2_LSB && ((code & 0b01u) == 0)) ||
                                               (renderMode == GfxRenderer::GRAY2_MSB && ((code & 0b10u) == 0))) {
                                             renderer.drawPixel(x, y, true);
                                           }
                                         }
                                       }
                                     },
                                     /*fastQuality=*/true);

      updateRefreshCadence();
      free(pageBuffer);
      Serial.printf("[%lu] [XTR] Rendered page %lu/%lu (2-bit high quality)\n", millis(), currentPage + 1,
                    xtc->getPageCount());
      return;
    }

    renderBwPreview();
    if (pagesUntilFullRefresh <= 1) {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      pagesUntilFullRefresh = SETTINGS.xtcRefreshFrequency;
    } else {
      renderer.displayBuffer();
      pagesUntilFullRefresh--;
    }

    renderer.clearScreen(0x00);
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) == 1) {
          renderer.drawPixel(x, y, false);
        }
      }
    }
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        const uint8_t pv = getPixelValue(x, y);
        if (pv == 1 || pv == 2) {
          renderer.drawPixel(x, y, false);
        }
      }
    }
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();

    renderer.clearScreen();
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) >= 1) {
          renderer.drawPixel(x, y, true);
        }
      }
    }

    renderer.cleanupGrayscaleWithFrameBuffer();

    free(pageBuffer);

    Serial.printf("[%lu] [XTR] Rendered page %lu/%lu (2-bit grayscale)\n", millis(), currentPage + 1,
                  xtc->getPageCount());
    return;
  } else {
    const size_t srcRowBytes = (pageWidth + 7) / 8;

    for (uint16_t srcY = 0; srcY < maxSrcY; srcY++) {
      const size_t srcRowStart = srcY * srcRowBytes;

      for (uint16_t srcX = 0; srcX < pageWidth; srcX++) {
        const size_t srcByte = srcRowStart + srcX / 8;
        const size_t srcBit = 7 - (srcX % 8);
        const bool isBlack = !((pageBuffer[srcByte] >> srcBit) & 1);

        if (isBlack) {
          renderer.drawPixel(srcX, srcY, true);
        }
      }
    }
  }

  free(pageBuffer);

  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.xtcRefreshFrequency;
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  Serial.printf("[%lu] [XTR] Rendered page %lu/%lu (%u-bit)\n", millis(), currentPage + 1, xtc->getPageCount(),
                bitDepth);
}

void XtcReaderActivity::saveProgress() const {
  FsFile f;
  if (SdMan.openFileForWrite("XTR", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = currentPage & 0xFF;
    data[1] = (currentPage >> 8) & 0xFF;
    data[2] = (currentPage >> 16) & 0xFF;
    data[3] = (currentPage >> 24) & 0xFF;
    f.write(data, 4);
    f.close();
  }
}

void XtcReaderActivity::loadProgress() {
  FsFile f;
  if (SdMan.openFileForRead("XTR", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      Serial.printf("[%lu] [XTR] Loaded progress: page %lu\n", millis(), currentPage);

      if (currentPage >= xtc->getPageCount()) {
        currentPage = 0;
      }
    }
    f.close();
  }
}

void XtcReaderActivity::ensureThumbnailExists() {
  if (!xtc) {
    return;
  }
  const std::string thumbPath = xtc->getThumbBmpPath();
  if (!SdMan.exists(thumbPath.c_str())) {
    xtc->generateThumbBmp();
  }
}

void XtcReaderActivity::initStats() {
  if (!xtc) {
    return;
  }

  if (loadBookStats(xtc->getCachePath().c_str(), bookStats)) {
    bookStats.sessionCount++;
  } else {
    bookStats.path = xtc->getCachePath();
    bookStats.title = xtc->getTitle();
    bookStats.author = xtc->getAuthor();
    bookStats.totalReadingTimeMs = 0;
    bookStats.totalPagesRead = 0;
    bookStats.totalChaptersRead = 0;
    bookStats.lastReadTimeMs = millis();
    bookStats.progressPercent = 0;
    bookStats.lastSpineIndex = static_cast<uint16_t>(chapterIndexForPage(*xtc, currentPage));
    bookStats.lastPageNumber = static_cast<uint16_t>(std::min<uint32_t>(currentPage, UINT16_MAX));
    bookStats.avgPageTimeMs = 0;
    bookStats.sessionCount = 1;
  }

  bookStats.lastReadTimeMs = millis();
  pageStartTime = millis();
  lastSaveTime = millis();
}

void XtcReaderActivity::startPageTimer() { pageStartTime = millis(); }

void XtcReaderActivity::endPageTimer() {
  if (pageStartTime == 0 || !xtc) {
    return;
  }

  const uint32_t now = millis();
  const uint32_t timeSpent = now - pageStartTime;

  if (timeSpent < 1000) {
    pageStartTime = 0;
    return;
  }

  const uint32_t pageCount = xtc->getPageCount();
  if (pageCount > 0 && currentPage < pageCount) {
    bookStats.totalReadingTimeMs += timeSpent;
    bookStats.totalPagesRead++;
    bookStats.lastReadTimeMs = now;
    bookStats.lastPageNumber = static_cast<uint16_t>(std::min<uint32_t>(currentPage, UINT16_MAX));
    bookStats.lastSpineIndex = static_cast<uint16_t>(chapterIndexForPage(*xtc, currentPage));
    bookStats.progressPercent = (static_cast<float>(currentPage) + 1.f) / static_cast<float>(pageCount) * 100.f;

    if (bookStats.totalPagesRead > 0) {
      bookStats.avgPageTimeMs = bookStats.totalReadingTimeMs / bookStats.totalPagesRead;
    }

    if (now - lastSaveTime >= STATS_SAVE_INTERVAL_MS) {
      saveBookStatsToFile();
      lastSaveTime = now;
    }
  }

  pageStartTime = 0;
}

void XtcReaderActivity::saveBookStatsToFile() {
  if (!xtc) {
    return;
  }
  bookStats.lastReadTimeMs = millis();
  bookStats.path = xtc->getCachePath();
  bookStats.title = xtc->getTitle();
  bookStats.author = xtc->getAuthor();
  ::saveBookStats(xtc->getCachePath().c_str(), bookStats);
}
