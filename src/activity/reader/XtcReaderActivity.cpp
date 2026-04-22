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

#include "state/SystemSetting.h"
#include "state/Session.h"
#include "system/MappedInputManager.h"
#include "state/RecentBooks.h"
#include "XtcReaderChapterSelectionActivity.h"
#include "system/Fonts.h"

namespace {
constexpr unsigned long skipPageMs = 700;
constexpr unsigned long goHomeMs = 1000;
constexpr unsigned long STATS_SAVE_INTERVAL_MS = 30000;

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
}  

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

  xTaskCreate(&XtcReaderActivity::taskTrampoline, "XtcReaderActivityTask",
              4096,               
              this,               
              1,                  
              &displayTaskHandle  
  );
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

  
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (xtc && xtc->hasChapters() && !xtc->getChapters().empty()) {
      endPageTimer();
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      enterNewActivity(new XtcReaderChapterSelectionActivity(
          this->renderer, this->mappedInput, xtc, currentPage,
          [this] {
            exitActivity();
            updateRequired = true;
            startPageTimer();
          },
          [this](const uint32_t newPage) {
            currentPage = newPage;
            exitActivity();
            updateRequired = true;
            startPageTimer();
          }));
      xSemaphoreGive(renderingMutex);
    }
  }

  
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= goHomeMs) {
    onGoToRecent();
    return;
  }

  
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < goHomeMs) {
    onGoBack();
    return;
  }

  
  const bool usePressForPageTurn = !SETTINGS.longPressChapterSkip;
  const bool prevTriggered = usePressForPageTurn ? (mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                                                    mappedInput.wasPressed(MappedInputManager::Button::Left))
                                                 : (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                                                    mappedInput.wasReleased(MappedInputManager::Button::Left));
  const bool powerPageTurn = SETTINGS.readerShortPwrBtn == SystemSetting::READER_SHORT_PWRBTN::READER_PAGE_TURN &&
                             mappedInput.wasReleased(MappedInputManager::Button::Power);
  const bool nextTriggered = usePressForPageTurn
                                 ? (mappedInput.wasPressed(MappedInputManager::Button::PageForward) || powerPageTurn ||
                                    mappedInput.wasPressed(MappedInputManager::Button::Right))
                                 : (mappedInput.wasReleased(MappedInputManager::Button::PageForward) || powerPageTurn ||
                                    mappedInput.wasReleased(MappedInputManager::Button::Right));

  if (!prevTriggered && !nextTriggered) {
    return;
  }

  if (xtc->getPageCount() == 0) {
    return;
  }

  const uint32_t pageCount = xtc->getPageCount();

  if (currentPage >= pageCount) {
    endPageTimer();
    currentPage = pageCount - 1;
    updateRequired = true;
    startPageTimer();
    return;
  }

  const bool skipPages = SETTINGS.longPressChapterSkip && mappedInput.getHeldTime() > skipPageMs;
  const int skipAmount = skipPages ? 10 : 1;

  if (prevTriggered) {
    endPageTimer();
    if (currentPage >= static_cast<uint32_t>(skipAmount)) {
      currentPage -= skipAmount;
    } else {
      currentPage = 0;
    }
    startPageTimer();
    updateRequired = true;
  } else if (nextTriggered) {
    endPageTimer();
    const uint32_t oldPage = currentPage;
    const int oldCh = chapterIndexForPage(*xtc, oldPage);
    currentPage += skipAmount;
    if (currentPage >= pageCount) {
      currentPage = pageCount;
    }
    const uint32_t refPage = std::min(currentPage, pageCount - 1);
    const int newCh = chapterIndexForPage(*xtc, refPage);
    if (newCh > oldCh) {
      bookStats.totalChaptersRead += static_cast<uint32_t>(newCh - oldCh);
    }
    startPageTimer();
    updateRequired = true;
  }
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
    
    renderer.clearScreen();
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 300, "End of book", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  renderPage();
  saveProgress();
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
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 300, "Memory error", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  
  size_t bytesRead = xtc->loadPage(currentPage, pageBuffer, pageBufferSize);
  if (bytesRead == 0) {
    Serial.printf("[%lu] [XTR] Failed to load page %lu\n", millis(), currentPage);
    free(pageBuffer);
    renderer.clearScreen();
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 300, "Page load error", true, EpdFontFamily::BOLD);
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

    
    

    
    uint32_t pixelCounts[4] = {0, 0, 0, 0};
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        pixelCounts[getPixelValue(x, y)]++;
      }
    }
    Serial.printf("[%lu] [XTR] Pixel distribution: White=%lu, DarkGrey=%lu, LightGrey=%lu, Black=%lu\n", millis(),
                  pixelCounts[0], pixelCounts[1], pixelCounts[2], pixelCounts[3]);
    const bool hasActualGrayscale = (pixelCounts[1] > 0 || pixelCounts[2] > 0);

    
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) >= 1) {
          renderer.drawPixel(x, y, true);
        }
      }
    }

    
    if (pagesUntilFullRefresh <= 1) {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    } else {
      renderer.displayBuffer();
      pagesUntilFullRefresh--;
    }

    if (hasActualGrayscale) {
      
      
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
    }

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
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
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
